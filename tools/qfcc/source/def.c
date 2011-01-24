/*
	def.c

	def management and symbol tables

	Copyright (C) 2002 Bill Currie <bill@taniwha.org>
	Copyright (C) 1996-1997  Id Software, Inc.

	Author: Bill Currie <bill@taniwha.org>
	Date: 2002/06/09

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

static __attribute__ ((used)) const char rcsid[] =
	"$Id$";

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <stdlib.h>

#include <QF/hash.h>
#include <QF/sys.h>
#include <QF/va.h>

#include "qfcc.h"
#include "def.h"
#include "defspace.h"
#include "diagnostic.h"
#include "expr.h"
#include "immediate.h"
#include "options.h"
#include "reloc.h"
#include "strpool.h"
#include "struct.h"
#include "type.h"

static def_t *free_defs;

static void
set_storage_bits (def_t *def, storage_class_t storage)
{
	switch (storage) {
		case st_system:
			def->system = 1;
			// fall through
		case st_global:
			def->global = 1;
			def->external = 0;
			def->local = 0;
			break;
		case st_extern:
			def->global = 1;
			def->external = 1;
			def->local = 0;
			break;
		case st_static:
			def->external = 0;
			def->global = 0;
			def->local = 0;
			break;
		case st_local:
			def->external = 0;
			def->global = 0;
			def->local = 1;
			break;
	}
	def->initialized = 0;
}

def_t *
new_def (const char *name, type_t *type, defspace_t *space,
		 storage_class_t storage)
{
	def_t      *def;

	ALLOC (16384, def_t, defs, def);

	if (!space && storage != st_extern)
		internal_error (0, "non-external def with no storage space");

	def->return_addr = __builtin_return_address (0);

	def->name = name ? save_string (name) : 0;
	def->type = type;
	def->space = space;

	if (storage != st_extern) {
		*space->def_tail = def;
		space->def_tail = &def->next;
		def->offset = defspace_new_loc (space, type_size (type));
	}

	def->file = pr.source_file;
	def->line = pr.source_line;

	set_storage_bits (def, storage);

	return def;
}

void
free_def (def_t *def)
{
	if (def->space) {
		def_t     **d;

		for (d = &def->space->defs; *d && *d != def; d = &(*d)->next)
			;
		*d = def->next;
		defspace_free_loc (def->space, def->offset, type_size (def->type));
	}
	def->next = free_defs;
	free_defs = def;
}

void
def_to_ddef (def_t *def, ddef_t *ddef, int aux)
{
	type_t     *type = def->type;

	if (aux)
		type = type->t.fldptr.type;	// aux is true only for fields
	ddef->type = type->type;
	ddef->ofs = def->offset;
	ddef->s_name = ReuseString (def->name);
}
