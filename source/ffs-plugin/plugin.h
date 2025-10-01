/*
 * FFS plugin for Custom IOS.
 *
 * Copyright (C) 2009-2010 Waninkoko.
 * Copyright (C) 2011 davebaol.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _PLUGIN_H_
#define _PLUGIN_H_

#include "fat.h"
#include "types.h"

/* Mode codes */
#define MODE_SDHC		0x01
#define MODE_USB		0x02
#define MODE_REV17		0x80
#define MODE_FULL		0x100


/* Config structure */
struct fsConfig {
	/* Mode */
	u32 mode;

	/* Nand path */
	char path[FAT_MAXPATH];

	/* Nand path length */
	u32 pathlen;
};

/* Extern */
extern struct fsConfig config;
extern u32 forceRealPath;

#endif
