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
#include "ioctls.h"

/* CControl library: provides colored memory allocations.
 * Tighly coupled with its Linux kernel module (in case of errors,
 * check that the library and module are in sync).
 * Warning: this library is NOT thread-safe.
 */

/** Block cyclic layout.
 * Defined in ccontrol/ioctls.h
 * cc_layout.color_list must be allocated manually.
 */
typedef struct cc_ioctl_config cc_layout;

/** Ccontrol module info.
 * Defined in ccontrol/ioctls.h
 */
typedef struct cc_ioctl_info cc_info;

/**
 * Colored area description struct.
 */
struct ccontrol_area {
	int fd; /** Area file descriptor. */
	void * start; /** mmaped region start. */
	size_t size; /** area size. */
	cc_info module_info; /** module info that will be filled at area creation. */
};

/**
 * Area creation.
 * @return new unconfigured area on success, NULL on error + errno.
 */
struct ccontrol_area * ccontrol_create (void);

/**
 * Area configuration
 * @param layout Layout description structure.
 * @return 0 on success, -1 on error + errno.
 */
int ccontrol_configure (struct ccontrol_area * area, cc_layout * layout);

/** Destroys an area.
 * @param area An area.
 * @return 0 on success, -1 on error + errno.
 */
int ccontrol_destroy (struct ccontrol_area * area);

#endif /* CCONTROL_H */
