/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * Copyright (C) 2010 Swann Perarnau <swann.perarnau@imag.fr>
 * Copyright (C) 2015 Francois Gindraud <francois.gindraud@inria.fr>
 */

/* ioctl codes availables in ccontrol:
 * CCONTROL_IO_INFO: get info on module
 * CCONTROL_IO_CONFIG: set area config (block cyclic coloring)
 */

#ifndef IOCTLS_H
#define IOCTLS_H

#include <linux/ioctl.h>

/* ioctl fixed major number */
#define CCONTROL_IO_MAGIC 250

struct cc_ioctl_info {
	int nb_colors; // number of colors used in the module
	int block_size; // size of a colored block in bytes
	int color_list_size_max; // maximum size of color list in config ioctl
};

struct cc_ioctl_config {
	/* block cyclic coloring:
	 * [color_list with color_repeat pages for each color] repeated list_repeat times
	 */
	int *color_list;
	int nb_colors;
	int color_repeat;
	int list_repeat;
};

#define CCONTROL_IO_INFO _IOW(CCONTROL_IO_MAGIC, 0, struct cc_ioctl_info *)
#define CCONTROL_IO_CONFIG _IOR(CCONTROL_IO_MAGIC, 1, struct cc_ioctl_config *)
#define CCONTROL_IO_NR 2

#endif /* IOCTLS_H */
