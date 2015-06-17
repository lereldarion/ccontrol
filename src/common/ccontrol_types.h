/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * Copyright (C) 2015 Francois Gindraud <francois.gindraud@inria.fr>
 */

#ifndef CCONTROL_TYPES_H
#define CCONTROL_TYPES_H

/** Ccontrol module info.
 */
struct cc_module_info {
	int nb_colors; // number of colors used in the module
	int block_size; // size of a colored block in bytes
	int color_list_size_max; // maximum size of color list in config ioctl
};

/** Block cyclic layout.
 * cc_layout.color_list must be allocated manually.
 * Layout = [color_list[0] * color_repeat, ..., color_list[nb_colors - 1] * color_repeat] * list_repeat
 */
struct cc_layout {
	int *color_list;
	int nb_colors;
	int color_repeat;
	int list_repeat;
};

#endif /* CCONTROL_TYPES_H */
