/*
	function.c

	QC function support code

	Copyright (C) 2002 Bill Currie

	Author: Bill Currie <bill@taniwha.org>
	Date: 2002/5/7

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
static const char rcsid[] =
	"$Id$";

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <stdlib.h>

#include "QF/dstring.h"

#include "qfcc.h"

#include "debug.h"
#include "def.h"
#include "emit.h"
#include "expr.h"
#include "function.h"
#include "immediate.h"
#include "opcodes.h"
#include "reloc.h"
#include "type.h"

static param_t *free_params;
static function_t *free_functions;

param_t *
new_param (const char *selector, type_t *type, const char *name)
{
	param_t    *param;

	ALLOC (4096, param_t, params, param);
	param->next = 0;
	param->selector = selector;
	param->type = type;
	param->name = name;

	return param;
}

param_t *
_reverse_params (param_t *params, param_t *next)
{
	param_t    *p = params;
	if (params->next)
		p = _reverse_params (params->next, params);
	params->next = next;
	return p;
}

param_t *
reverse_params (param_t *params)
{
	if (!params)
		return 0;
	return _reverse_params (params, 0);
}

type_t *
parse_params (type_t *type, param_t *parms)
{
	param_t    *p;
	type_t      new;

	memset (&new, 0, sizeof (new));
	new.type = ev_func;
	new.aux_type = type;
	new.num_parms = 0;

	for (p = parms; p; p = p->next) {
		if (new.num_parms > MAX_PARMS) {
			error (0, "too many params");
			return type;
		}
		if (!p->selector && !p->type && !p->name) {
			if (p->next) {
				error (0, "internal error");
				abort ();
			}
			new.num_parms = -(new.num_parms + 1);
		} else if (p->type) {
			new.parm_types[new.num_parms] = p->type;
			new.num_parms++;
		}
	}
	//print_type (&new); puts("");
	return find_type (&new);
}

void
build_scope (function_t *f, def_t *func, param_t *params)
{
	int         i;
	def_t      *def;
	param_t    *p;
	def_t      *argv = 0;
	int         parm_ofs[MAX_PARMS];

	f->scope = new_scope (sc_params, new_defspace (), pr.scope);

	if (func->type->num_parms < 0) {
		def = get_def (&type_integer, ".argc", f->scope, st_local);
		def->used = 1;
		def_initialized (def);
		argv = get_def (&type_pointer, ".argv", f->scope, st_local);
		argv->used = 1;
		def_initialized (argv);
	}

	for (p = params, i = 0; p; p = p->next) {
		if (!p->selector && !p->type && !p->name)
			continue;					// ellipsis marker
		if (!p->type)
			continue;					// non-param selector
		def = get_def (p->type, p->name, f->scope, st_local);
		parm_ofs[i] = def->ofs;
		if (i > 0 && parm_ofs[i] < parm_ofs[i - 1]) {
			error (0, "bad parm order");
			abort ();
		}
		//printf ("%s%s %d\n", p == params ? "" : "    ", p->name, def->ofs);
		def->used = 1;				// don't warn for unused params
		def_initialized (def);	// params are assumed to be initialized
		i++;
	}

	if (argv) {
		while (i < MAX_PARMS) {
			def = get_def (&type_vector, 0, f->scope, st_local);
			def->used = 1;
			if (argv->type == &type_pointer)
				argv->type = array_type (&type_vector, MAX_PARMS - i);
			i++;
		}
	}
}

function_t *
new_function (const char *name)
{
	function_t	*f;

	ALLOC (1024, function_t, functions, f);

	*pr.func_tail = f;
	pr.func_tail = &f->next;
	f->function_num = pr.num_functions++;
	f->s_name = ReuseString (name);
	f->s_file = pr.source_file;
	return f;
}

function_t *
build_builtin_function (def_t *def, expr_t *bi_val)
{
	function_t *f;

	if (def->type->type != ev_func) {
		error (bi_val, "%s is not a function", def->name);
		return 0;
	}
	
	if (bi_val->type != ex_integer && bi_val->type != ex_float) {
		error (bi_val, "invalid constant for = #");
		return 0;
	}

	f = new_function (def->name);

	f->builtin = bi_val->type == ex_integer ? bi_val->e.integer_val
											: (int)bi_val->e.float_val;
	f->def = def;
	reloc_def_func (f, def->ofs);
	build_function (f);
	finish_function (f);
	return f;
}

void
build_function (function_t *f)
{
	f->def->constant = 1;
	f->def->initialized = 1;
	G_FUNCTION (f->def->ofs) = f->function_num;
}

void
finish_function (function_t *f)
{
	if (f->aux) {
		def_t *def;
		f->aux->function = f->function_num;
		for (def = f->scope->head; def; def = def->def_next) {
			if (def->name) {
				ddef_t *d = new_local ();
				d->type = def->type->type;
				d->ofs = def->ofs;
				d->s_name = ReuseString (def->name);

				f->aux->num_locals++;
			}
		}
	}
}

void
emit_function (function_t *f, expr_t *e)
{
	//printf (" %s =\n", f->def->name);

	if (f->aux)
		lineno_base = f->aux->source_line;

	current_scope = f->scope;
	while (e) {
		//printf ("%d ", pr.source_line);
		//print_expr (e);
		//puts("");

		emit_expr (e);
		e = e->next;
	}
	emit_statement (0, op_done, 0, 0, 0);
	flush_scope (current_scope, 0);
	current_scope = pr.scope;
	reset_tempdefs ();

	//puts ("");
}

int
function_parms (function_t *f, byte *parm_size)
{
	int         count, i;

	if (f->def->type->num_parms >= 0)
		count = f->def->type->num_parms;
	else
		count = -f->def->type->num_parms - 1;

	for (i = 0; i < count; i++)
		parm_size[i] = type_size (f->def->type->parm_types[i]);
	return f->def->type->num_parms;
}
