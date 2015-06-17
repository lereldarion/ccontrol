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

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>

#define ERROR_AT(s, ...) error_at_line (0, errno, __FILE__, __LINE__, s,##__VA_ARGS__)

/* Area creation / destruction
 *
 * The kernel automatically closes any open file descriptor at program end.
 * Closing an area file descriptor will destroy the module area.
 * So there is no need to garbage collect opened areas.
 */

struct ccontrol_area * ccontrol_create (void) {
	struct ccontrol_area * area = malloc (sizeof (struct ccontrol_area));
	if (area == NULL) {
		ERROR_AT ("malloc");
		return NULL;
	}

	area->size = 0;
	area->start = NULL;

	// create new area by opening ccontrol device
	area->fd = open ("/dev/ccontrol", O_RDWR); 
	if (area->fd != -1) {
		// get module info
		if (ioctl (area->fd, CCONTROL_IO_INFO, &area->module_info) == 0) {
			return area;
		} else {
			ERROR_AT ("ccontrol device info");
		}
		close (area->fd);
	} else {
		ERROR_AT ("ccontrol device open");
	}
	free (area);
	return NULL;
}

int ccontrol_configure (struct ccontrol_area * area, struct cc_layout * layout) {
	if (area == NULL || layout == NULL || layout->color_list == NULL ||
			layout->nb_colors < 1 || layout->color_repeat < 1 || layout->list_repeat < 1) {
		errno = EINVAL;
		return -1;
	}

	if (ioctl (area->fd, CCONTROL_IO_CONFIG, layout) < 0) {
		ERROR_AT ("area configure");
		return -1;
	}

	area->size = layout->nb_colors * layout->color_repeat * layout->list_repeat * area->module_info.block_size;
	area->start = mmap (NULL, area->size, PROT_READ | PROT_WRITE, MAP_SHARED, area->fd, 0);
	if (area->start == MAP_FAILED) {
		ERROR_AT ("area mmap");
		area->start = NULL;
		return -1;
	}
	return 0;
}

int ccontrol_destroy (struct ccontrol_area * area) {
	if (area == NULL) {
		errno = EINVAL;
		return -1;
	}
	int err = 0;

	if (area->start != NULL) {
		err = munmap (area->start, area->size);
		if (err)
			ERROR_AT ("area munmap");
	}
	if (close (area->fd) == -1) {
		err = -1;
		ERROR_AT ("ccontrol device close");
	}
	free (area);
	return err;
}

