/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * Copyright (C) 2010 Swann Perarnau <swann.perarnau@imag.fr>
 * Copyright (C) 2015 Francois Gindraud <francois.gindraud@inria.fr>
 */
#ifndef CCONTROL_H
#define CCONTROL_H 1

#include <stdlib.h>

#include "colorset.h"

/* CControl library: provides colored memory allocations.
 * Tighly coupled with its Linux kernel module (in case of errors,
 * check that the library and module are in sync).
 * Warning: this library is NOT thread-safe.
 */

/**
 * Colored area description struct.
 * ADT.
 */
struct ccontrol_area;

/**
 * Area creation.
 * @param size size of area.
 * @param colors color set.
 */
struct ccontrol_area * ccontrol_create (size_t size, color_set * colors);

/** Destroys an area.
 */
int ccontrol_destroy (struct ccontrol_area * area);

/* Accessors
 */
size_t ccontrol_area_size (struct ccontrol_area * area);
void * ccontrol_area_start (struct ccontrol_area * area);
int ccontrol_area_color_of (struct ccontrol_area * area, void * ptr);

/* malloc interface
 * Not thread safe !
 */
void * ccontrol_malloc (struct ccontrol_area * area, size_t size);
void ccontrol_free (struct ccontrol_area * area, void * ptr);
void * ccontrol_realloc (struct ccontrol_area * area, void * ptr, size_t size);

/* environment variables names */
#define CCONTROL_ENV_PARTITION_COLORSET "CCONTROL_PSET"
#define CCONTROL_ENV_SIZE "CCONTROL_SIZE"


/* translate string to size
 * format is similar to kernel args : 1k 1M 1G
 * upper and lower cases work
 */
int ccontrol_str2size (size_t *s, char *str);


/* translate string to color_set
 * format is like cpusets : "1-4,5"
 */
int ccontrol_str2cset (color_set * c, char * str);

#endif /* CCONTROL_H */
