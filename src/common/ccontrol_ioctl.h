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

#ifndef CCONTROL_IOCTL_H
#define CCONTROL_IOCTL_H

#include <linux/ioctl.h>
#include "ccontrol_types.h"

/* ioctl fixed major number */
#define CCONTROL_IO_MAGIC 250

#define CCONTROL_IO_INFO _IOW(CCONTROL_IO_MAGIC, 0, struct cc_module_info *)
#define CCONTROL_IO_CONFIG _IOR(CCONTROL_IO_MAGIC, 1, struct cc_layout *)
#define CCONTROL_IO_NR 2

#endif /* CCONTROL_IOCTL_H */
