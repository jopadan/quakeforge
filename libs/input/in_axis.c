/*
	in_axis.c

	Logical axis support

	Copyright (C) 2021 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2021/10/1

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

*/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "QF/cmd.h"
#include "QF/hash.h"
#include "QF/input.h"
#include "QF/sys.h"

static hashtab_t *axis_tab;

static const char *
axis_get_key (const void *a, void *data)
{
	__auto_type axis = (const in_axis_t *) a;
	return axis->name;
}

static void
axis_free (void *a, void *data)
{
}

VISIBLE int
IN_RegisterAxis (in_axis_t *axis)
{
	const char *name = axis->name;
	if (Hash_Find (axis_tab, name)) {
		return 0;
	}

	Hash_Add (axis_tab, axis);
	return 1;
}

static void __attribute__((constructor))
in_axis_init (void)
{
	axis_tab = Hash_NewTable (127, axis_get_key, axis_free, 0, 0);
}
