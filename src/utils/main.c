/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * Copyright (C) 2010 Swann Perarnau <swann.perarnau@imag.fr>
 * Copyright (C) 2015 Francois Gindraud <francois.gindraud@inria.fr>
 */

/* small executable to load/unload and ld_preload a binary */

#include "config.h"
#include <ccontrol.h>
#include <errno.h>
#include <error.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

char * arg_max_mem = "1M";
int arg_colors = -1;
int arg_is_color_cache_level = 0;

/* utils */

size_t checked_strtoul (const char * str, int * valid, char ** endp) {
	char * endp_;
	if (endp == NULL)
		endp = &endp_;
	size_t r = strtoul (str, endp, 0);
	*valid = *endp > str;
	if (! *valid)
		error (0, errno, "unable to parse positive number \"%s\"", str);
	return r;
}

static size_t pretty_size(char *suffix, size_t size)
{
	const size_t scale = 1 << 10;
	const char suffixes[] = "BkMGTP";
	int i;
	for (i = 0; suffixes[i + 1] != '\0' && size >= scale && size % scale == 0; ++i)
		size = (size + scale - 1) / scale;
	*suffix = suffixes[i];
	return size;
}

static size_t str2size (char * str, int * valid) {
	char * endp;
	size_t r = checked_strtoul (str, valid, &endp);
	if (! *valid)
		return 0;
	switch (*endp) {
		case 'g':
		case 'G':
			r <<= 10;
		case 'm':
		case 'M':
			r <<= 10;
		case 'k':
		case 'K':
			r <<= 10;
		default:
			break;
	}
	return r;
}

/* scan /sys/devices/system and get data cache info
 */
#define SYSPATH "/sys/devices/system/cpu/cpu0/cache"

struct cache_info {
	size_t size;
	int assoc;
	int nb_colors;
	int found;
	const char * type;
};
static struct cache_info * caches = NULL;
static int nb_cache_levels = 0;

static int scandir_filter (const struct dirent * file) {
	static const char * cache_entry_prefix = "index";
	return strncmp (cache_entry_prefix, file->d_name, strlen (cache_entry_prefix)) == 0;
}

#define BUF_SIZE 150
static int read_sys_cache_file (const char * prefix, const char * name, char * buf) {
	int r = -1;
	char filename[BUF_SIZE];
	assert (snprintf (filename, BUF_SIZE, "%s/%s/%s", SYSPATH, prefix, name) > 0);
	FILE * f = fopen (filename, "r");
	if (f != NULL) {
		if (fgets (buf, BUF_SIZE - 1, f) != NULL) {
			r = 0;
			char * newline = strchr (buf, '\n');
			if (newline != NULL)
				*newline = '\0';
		} else {
			error (0, errno, "unable to read %s", filename);
		}
		if (fclose (f) < 0)
			error (0, errno, "closing %s failed", filename);
	} else {
		error (0, errno, "opening %s failed", filename);
	}
	return r;
}

static int scan_sys_cache_info (void) {
	long page_size = sysconf (_SC_PAGESIZE);
	if (page_size <= 0) {
		error (0, errno, "unable to get pagesize");
		return -1;
	}

	/* versionsort sort by index number */
	struct dirent ** list;
	int nb_dir = scandir (SYSPATH, &list, scandir_filter, versionsort);
	if (nb_dir < 0) {
		error (0, errno, "scandir(%s)", SYSPATH);
		return -1;
	}

	// alloc table (will set found to 0)
	caches = calloc (nb_dir, sizeof (struct cache_info));
	if (caches == NULL)
		error (EXIT_FAILURE, errno, "calloc");
	nb_cache_levels = nb_dir;

	for (int i = 0; i < nb_dir; i++) {
		const char * d = list[i]->d_name;
		int ok;
		char buf[BUF_SIZE];
		struct cache_info cinfo;

		// ensure type includes Data
		if (read_sys_cache_file (d, "type", buf) == -1)
			continue;
		if (strcmp (buf, "Data") == 0)
		  cinfo.type = "Data";
		else if (strcmp (buf, "Unified") == 0)
			cinfo.type = "Unified";
		else
			continue;

		// read size, assoc, level
		if (read_sys_cache_file (d, "size", buf) == -1)
			continue;
		cinfo.size = str2size (buf, &ok); 
		if (!(ok && cinfo.size > 0))
			continue;

		if (read_sys_cache_file (d, "ways_of_associativity", buf) == -1)
			continue;
		cinfo.assoc = checked_strtoul (buf, &ok, NULL);
		if (! (ok && cinfo.assoc > 0))
			continue;

		if (read_sys_cache_file (d, "level", buf) == -1)
			continue;
		int level = checked_strtoul (buf, &ok, NULL);
		if (! (ok && level < nb_cache_levels))
			continue;

		cinfo.nb_colors = cinfo.size / (page_size * cinfo.assoc);
		cinfo.found = 1;
		caches[level] = cinfo;
	}
	return 0;
}

static int get_nb_color (void) {
	// use manual setting
	if (arg_colors > 0 && !arg_is_color_cache_level) {
		printf ("Using manual color number = %d\n", arg_colors);
		return arg_colors;
	}

	// use autodetect : get cache info
	int l = arg_colors;
	if (scan_sys_cache_info () != 0)
		error (EXIT_FAILURE, 0, "unable to get cache information");

	// guided autodetect
	if (l >= 0 && arg_is_color_cache_level) {
		if (l < nb_cache_levels && caches[l].found) {
			printf ("Using L%d color setting = %d\n", l, caches[l].nb_colors);
			return caches[l].nb_colors;
		} else {
			printf ("L%d cache information not found, using LLC\n", arg_colors);
		}
	}

	// LLC autodetect
	for (l = nb_cache_levels - 1; l >= 0; --l)
		if (caches[l].found) {
			printf ("Using L%d (detected LLC) color setting = %d\n", l, caches[l].nb_colors);
			return caches[l].nb_colors;
		}

	error (EXIT_FAILURE, 0, "no cache info detected");
	return -1;
}

/* commands:
 * load: load the kernel module
 * unload: unload the kernel module
 * info: print cache stats
 */
static int load_module (void) {
	char argm[80], argc[80];
	assert (snprintf (argm, 80, "max_mem=%s", arg_max_mem) > 0);
	assert (snprintf (argc, 80, "nb_colors=%lu", get_nb_color ()) > 0);

	printf ("Loading module using \"modprobe ccontrol %s %s\"\n", argm, argc);
	if (execlp ("modprobe", "modprobe", "ccontrol", argm, argc, NULL) < 0)
		error (EXIT_FAILURE, errno, "execlp modprobe");
	return EXIT_FAILURE; // should never be reached
}

static int unload_module (void) {
	printf ("Unloading module using \"modprobe -r ccontrol\"\n");
	if (execlp ("modprobe", "modprobe", "-r", "ccontrol", NULL) < 0)
		error (EXIT_FAILURE, errno, "execlp modprobe -r");
	return EXIT_FAILURE; // should never be reached
}

static int cmd_info(void)
{
	if (scan_sys_cache_info () != 0)
		error (EXIT_FAILURE, 0, "unable to get cache data");

	printf ("%-6s %10s %10s %10s %10s\n", "level", "type", "size", "assoc", "colors");
	for (int level = 0; level < nb_cache_levels; ++level) {
		struct cache_info * i = &caches[level];
		if (i->found) {
			char sx; size_t sz;
			sz = pretty_size (&sx, i->size);
			printf ("L%-5d %10s %9zu%c %10d %10d\n", level, i->type, sz, sx, i->assoc, i->nb_colors);
		}
	}
	return EXIT_SUCCESS;
}

/* command line helpers */
void print_help (void) {
	printf ("Usage: ccontrol [options] <cmd> <args>\n\n");
	printf ("Available options:\n");
	printf ("--help,-h                      : print this help message\n");
	printf ("--version,-V                   : print program version\n");
	printf ("--max_mem,-m <string>          : maximum memory allocated to the module\n");
	printf ("--colors,-c <uint/\"L<int>\">  : colors used by the module\n");
	printf ("Available commands:\n");
	printf ("load                           : load kernel module\n");
	printf ("unload                         : unload kernel module\n");
	printf ("info                           : print cache information\n");
}

/* command line arguments */
int main (int argc, char * argv[]) {
	int ask_help = 0;
	int ask_version = 0;
	struct option long_options[] = {
		{ "help", no_argument, &ask_help, 1},
		{ "version", no_argument, &ask_version, 1},
		{ "max_mem", required_argument, NULL, 'm' },
		{ "colors", required_argument, NULL, 'c' },
		{ 0, 0 , 0, 0},
	};
	const char * short_opts ="hVm:c:";
	int c;
	int option_index = 0;

	// parse options
	while (1) {
		c = getopt_long (argc, argv, short_opts, long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case 0:
				break;
			case 'c':
				{
					char * opt = optarg;
					if (opt[0] == 'L') {
						arg_is_color_cache_level = 1;
						opt++;
					}
					arg_colors = atoi (opt);
					if (arg_colors < 0)
						error (EXIT_FAILURE, 0, "invalid --colors value \"%s\"", optarg);
				}
				break;
			case 'm':
				arg_max_mem = optarg;
				break;
			case 'h':
				ask_help = 1;
				break;
			case 'V':
				ask_version = 1;
				break;
			default:
				error (EXIT_FAILURE, 0, "getopt: unknown option code '%c' (%d)", c, c);
				break;
		}
	}
	// forget the parsed part of argv
	argc -= optind;
	argv = &argv[optind];

	if (ask_version) {
		printf ("ccontrol: version %s\n", PACKAGE_STRING);
		return EXIT_SUCCESS;
	}

	if (ask_help || argc == 0) {
		print_help ();
		return EXIT_SUCCESS;
	}

	if (strcmp (argv[0], "info") == 0) {
		return cmd_info ();
	} else if (strcmp (argv[0], "load") == 0) {
		return load_module ();
	} else if (strcmp (argv[0], "unload") == 0) {
		return unload_module();
	} else {
		fprintf (stderr, "unknown command \"%s\"\n", argv[0]);
		print_help ();
		return EXIT_FAILURE;
	}
}
