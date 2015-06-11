/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * Copyright (C) 2010 Swann Perarnau <swann.perarnau@imag.fr>
 * Copyright (C) 2015 Francois Gindraud <francois.gindraud@inria.fr>
 */
#include "ccontrol.h"
#include "freelist.h"
#include "ioctls.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <error.h>

//#include <string.h>

#define FATAL_ERROR_AT(s, ...) error_at_line (EXIT_FAILURE, errno, __FILE__, __LINE__, s,##__VA_ARGS__)
#define ERROR_AT(s, ...) error_at_line (0, errno, __FILE__, __LINE__, s,##__VA_ARGS__)

struct ccontrol_area {
	struct ccontrol_area * next; /* list of opened areas for cleanup */
	dev_t dev; /* colored device number */
	int fd; /* file descriptor of colored device */
	void * start; /* mmaped region start */
	size_t size; /* area size */
};

/* lib utils */

#define CONTROL_DEVICE "/dev/ccontrol"

#if defined(P_tmpdir)
#define COLORED_DEVICE_DIR P_tmpdir
#else
#define COLORED_DEVICE_DIR "/tmp"
#endif

static void colored_device_name (char * buf, int buf_size, dev_t dev) {
	int snp = snprintf (buf, buf_size, "%s/ccontrol-colored-%d", COLORED_DEVICE_DIR, minor (dev));
	assert (snp >= 0 && snp <= buf_size);
}

/* ccontrol control device will be opened only once, and closed at end of program.
 * It also acts as an init flag, with lazy initialization (by get_control_fd)
 *
 * opened_areas stores alive areas for collection at program end.
 * ccontrol_create adds its area to the list, and ccontrol_destroy removes it.
 */
static int control_device_fd = -1;
static struct ccontrol_area * opened_areas = NULL;

static void lib_cleanup (void) {
	while (opened_areas != NULL)
		ccontrol_destroy (opened_areas);

	if (control_device_fd != -1) {
		close (control_device_fd);
		control_device_fd = -1;
	}
}

static int get_control_fd (void) {
	if (control_device_fd == -1) {
		// register cleanup function
		if (atexit (lib_cleanup) != 0)
			FATAL_ERROR_AT ("registering lib cleanup function");
		// open control device
		control_device_fd = open (CONTROL_DEVICE, O_RDWR | O_NONBLOCK);
		if (control_device_fd == -1)
			FATAL_ERROR_AT ("control device open (%s)", CONTROL_DEVICE);
	}
	return control_device_fd;
}

/* area creation / destruction */

struct ccontrol_area * ccontrol_create (size_t size, color_set * colors) {
	if(colors == NULL || size == 0)
		return NULL;
	int control_fd = get_control_fd ();
	struct ccontrol_area * area = malloc (sizeof (struct ccontrol_area));
	if (area == NULL) {
		ERROR_AT ("malloc");
		return NULL;
	}
	area->size = size;

	ioctl_args io_args = {
		.size = size,
		.c = *colors
	};
	if (ioctl(control_fd, IOCTL_NEW, &io_args) == 0) {
		// create colored device file for the new colored device
		const int buf_size = 200; char buf[buf_size];
		area->dev = makedev (io_args.major, io_args.minor);
		colored_device_name (buf, buf_size, area->dev);
		if (mknod (buf, S_IFCHR | S_IRUSR | S_IWUSR, area->dev) == 0) {
			// open device
			area->fd = open (buf, O_RDWR);
			if (area->fd != -1) {
				// mmap colored device
				area->start = mmap (NULL, area->size, PROT_READ | PROT_WRITE, MAP_SHARED, area->fd, 0); // FIXME private mapping ?
				if (area->start != MAP_FAILED) {
					// TODO remove
					fl_init(area->start, size); // init freelist

					// add to cleanup list
					area->next = opened_areas;
					opened_areas = area;

					return area;
				} else {
					ERROR_AT ("colored device mmap (f=%s, s=%lu)", buf, size);
				}
				close (area->fd);
			} else {
				ERROR_AT ("colored device open (f=%s)", buf);
			}
			unlink (buf);
		} else {
			ERROR_AT ("colored device mknod (f=%s, dev=%d:%d)", buf, io_args.major, io_args.minor);
		}
		ioctl (control_fd, IOCTL_FREE, &io_args);
	} else {
		ERROR_AT ("control device ioctl_new");
	}
	free (area);
	return NULL;
}

int ccontrol_destroy (struct ccontrol_area * area)
{
	if (area == NULL)
		return -1;
	int err = 0;

	// unmap, close and delete colored device file
	if (munmap (area->start, area->size) == -1) {
		err = -1;
		ERROR_AT ("colored device munmap");
	}
	if (close (area->fd) == -1) {
		err = -1;
		ERROR_AT ("colored device close");
	}
	const int buf_size = 200; char buf[buf_size];
	colored_device_name (buf, buf_size, area->dev);
	if (unlink (buf) == -1) {
		err = -1;
		ERROR_AT ("colored device unlink (f=%s)", buf);
	}

	// destroy device file
	ioctl_args io_args = {
		.major = major (area->dev),
		.minor = minor (area->dev)
	};
	if (ioctl (get_control_fd (), IOCTL_FREE, &io_args) == -1) {
		err = -1;
		ERROR_AT ("control device ioctl_free");
	}

	// remove from opened list
	struct ccontrol_area * it = opened_areas;
	struct ccontrol_area ** next_ptr = &opened_areas; 
	while (it != NULL) {
		if (it == area) {
			*next_ptr = it->next;
			break;
		}
		next_ptr = &it->next;
		it = it->next;
	}

	free (area);
	return err;
}

/* area accessors */

size_t ccontrol_area_size (struct ccontrol_area * area) {
	if (area == NULL)
		return 0;
	else
		return area->size;
}
void * ccontrol_area_start (struct ccontrol_area * area) {
	if (area == NULL)
		return NULL;
	else
		return area->start;
}
int ccontrol_area_color_of (struct ccontrol_area * area, void * ptr) {
	return -1; // TODO useful ?
}

/* malloc interface */

void * ccontrol_malloc (struct ccontrol_area * area, size_t size) {
	if(area == NULL)
		return NULL;
	return fl_allocate (area->start, size);
}

void ccontrol_free (struct ccontrol_area * area, void * ptr) {
	if(area == NULL)
		return NULL;
	fl_free (area->start, ptr);
}

void * ccontrol_realloc (struct ccontrol_area * area, void * ptr, size_t size) {
	if(area == NULL)
		return NULL;
	return fl_realloc (area->start, ptr, size);
}

/* Utils */

int ccontrol_str2cset (color_set * c, char * str) {
	unsigned long a,b;
	if (str == NULL || c == NULL)
		return 1;
	COLOR_ZERO (c);
	do {
		if (!isdigit (*str))
			return 1;
		errno = 0;
		b = a = strtoul (str, &str, 0);
		if (errno)
			return 1;
		if (*str == '-') {
			str++;
			if (!isdigit (*str))
				return 1;
			errno = 0;
			b = strtoul (str, &str, 0);
			if (errno)
				return 1;
		}
		if (a > b)
			return 1;
		if (b >= COLOR_SETSIZE)
			return 1;
		while (a <= b) {
			COLOR_SET (a, c);
			a++;
		}
		if (*str == ',')
			str++;
	} while (*str != '\0');
	return 0;
}

int ccontrol_str2size (size_t *s, char *str) {
	char *endp;
	unsigned long r;
	if(s == NULL || str == NULL)
		return 1;
	errno = 0;
	r = strtoul(str,&endp,0);
	if(errno)
		return errno;
	switch(*endp) {
		case 'g':
		case 'G':
			r<<=10;
		case 'm':
		case 'M':
			r<<=10;
		case 'k':
		case 'K':
			r<<=10;
		default:
			break;
	}
	*s = r;
	return 0;
}

