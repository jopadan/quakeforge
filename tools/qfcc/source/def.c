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

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <stdlib.h>

#include "QF/alloc.h"
#include "QF/hash.h"
#include "QF/sys.h"
#include "QF/va.h"

#include "qfcc.h"
#include "class.h"
#include "def.h"
#include "defspace.h"
#include "diagnostic.h"
#include "emit.h"
#include "expr.h"
#include "function.h"
#include "options.h"
#include "reloc.h"
#include "shared.h"
#include "strpool.h"
#include "struct.h"
#include "symtab.h"
#include "type.h"
#include "value.h"

typedef struct element_s {
	struct element_s *next;
	int         offset;
	type_t     *type;
	expr_t     *expr;
} element_t;

typedef struct element_chain_s {
	element_t  *head;
	element_t **tail;
} element_chain_t;

static def_t *defs_freelist;
static element_t *elements_freelist;

static element_t *
new_element (void)
{
	element_t  *element;
	ALLOC (256, element_t, elements, element);
	return element;
}

static element_t *
append_element (element_chain_t *element_chain, element_t *element)
{
	element->next = 0;
	*element_chain->tail = element;
	element_chain->tail = &element->next;
	return element;
}

static void
set_storage_bits (def_t *def, storage_class_t storage)
{
	switch (storage) {
		case sc_system:
			def->system = 1;
			// fall through
		case sc_global:
			def->global = 1;
			def->external = 0;
			def->local = 0;
			def->param = 0;
			break;
		case sc_extern:
			def->global = 1;
			def->external = 1;
			def->local = 0;
			def->param = 0;
			break;
		case sc_static:
			def->external = 0;
			def->global = 0;
			def->local = 0;
			def->param = 0;
			break;
		case sc_local:
			def->external = 0;
			def->global = 0;
			def->local = 1;
			def->param = 0;
			break;
		case sc_param:
			def->external = 0;
			def->global = 0;
			def->local = 1;
			def->param = 1;
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

	def->return_addr = __builtin_return_address (0);

	def->name = name ? save_string (name) : 0;
	def->type = type;

	def->file = pr.source_file;
	def->line = pr.source_line;

	set_storage_bits (def, storage);

	if (space) {
		if (space->type == ds_virtual && storage == sc_static)
			internal_error (0, "static in a virtual space");
		if (space->type != ds_virtual
			&& (storage == sc_param || storage == sc_local))
			internal_error (0, "param or local in a non-virtual space");
		def->space = space;
		*space->def_tail = def;
		space->def_tail = &def->next;
	}

	if (!type)
		return def;

	if (!space && storage != sc_extern)
		internal_error (0, "non-external def with no storage space");

	if (obj_is_class (type)) {
		error (0, "statically allocated instance of class %s",
			   type->t.class->name);
		return def;
	}

	if (storage != sc_extern) {
		int         size = type_size (type);
		int         alignment = type->alignment;

		if (!size) {
			error (0, "%s has incomplete type", name);
			size = 1;
			alignment = 1;
		}
		if (alignment < 1) {
			print_type (type);
			internal_error (0, "temp type has no alignment");
		}
		def->offset = defspace_alloc_aligned_loc (space, size, alignment);
	}

	return def;
}

def_t *
alias_def (def_t *def, type_t *type, int offset)
{
	def_t      *alias;

	if (def->alias) {
		expr_t      e;
		e.file = def->file;
		e.line = def->line;
		internal_error (&e, "aliasing an alias def");
	}
	if (type_size (type) > type_size (def->type))
		internal_error (0, "aliasing a def to a larger type");
	if (offset < 0 || offset + type_size (type) > type_size (def->type))
		internal_error (0, "invalid alias offset");
	if (type == def->type)
		return def;
	for (alias = def->alias_defs; alias; alias = alias->next) {
		if (alias->type == type && alias->offset == offset)
			return alias;
	}
	ALLOC (16384, def_t, defs, alias);
	alias->name = save_string (va ("[%s:%d]", def->name, offset));
	alias->return_addr = __builtin_return_address (0);
	alias->offset = offset;
	alias->offset_reloc = 1;
	alias->type = type;
	alias->alias = def;
	alias->line = pr.source_line;
	alias->file = pr.source_file;
	alias->next = def->alias_defs;
	def->alias_defs = alias;
	return alias;
}

def_t *
temp_def (type_t *type)
{
	def_t      *temp;
	defspace_t *space = current_func->symtab->space;
	int         size = type_size (type);
	int         alignment = type->alignment;

	if (size < 1 || size > 4) {
		internal_error (0, "%d invalid size for temp def", size);
	}
	if (alignment < 1) {
		internal_error (0, "temp type has no alignment");
	}
	if ((temp = current_func->temp_defs[size - 1])) {
		current_func->temp_defs[size - 1] = temp->temp_next;
		temp->temp_next = 0;
	} else {
		ALLOC (16384, def_t, defs, temp);
		temp->offset = defspace_alloc_aligned_loc (space, size, alignment);
		*space->def_tail = temp;
		space->def_tail = &temp->next;
		temp->name = save_string (va (".tmp%d", current_func->temp_num++));
	}
	temp->return_addr = __builtin_return_address (0);
	temp->type = type;
	temp->file = pr.source_file;
	temp->line = pr.source_line;
	set_storage_bits (temp, sc_local);
	temp->space = space;
	return temp;
}

void
free_temp_def (def_t *temp)
{
	int         size = type_size (temp->type) - 1;
	temp->temp_next = current_func->temp_defs[size];
	current_func->temp_defs[size] = temp;
}

void
free_def (def_t *def)
{
	if (!def->return_addr)
		internal_error (0, "double free of def");
	if (def->alias) {
		def_t     **a;

		// pull the alias out of the base def's list of alias defs to avoid
		// messing up the list when the alias def is freed.
		for (a = &def->alias->alias_defs; *a; a = &(*a)->next) {
			if (*a == def) {
				*a = def->next;
				break;
			}
		}
	} else if (def->space) {
		def_t     **d;

		for (d = &def->space->defs; *d && *d != def; d = &(*d)->next)
			;
		if (!*d)
			internal_error (0, "freeing unlinked def %s", def->name);
		*d = def->next;
		if (&def->next == def->space->def_tail)
			def->space->def_tail = d;
		if (!def->external)
			defspace_free_loc (def->space, def->offset, type_size (def->type));
	}
	def->return_addr = 0;
	def->free_addr = __builtin_return_address (0);
	FREE (defs, def);
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

static int
zero_memory (expr_t *local_expr, def_t *def, type_t *zero_type,
			 int init_size, int init_offset)
{
	int         zero_size = type_size (zero_type);
	expr_t     *zero = convert_nil (new_nil_expr (), zero_type);
	expr_t     *dst;

	for (; init_offset < init_size + 1 - zero_size; init_offset += zero_size) {
		dst = new_pointer_expr (init_offset, zero_type, def);
		append_expr (local_expr, assign_expr (unary_expr ('.', dst), zero));
	}
	return init_offset;
}

static void
init_elements_nil (def_t *def)
{
	if (def->local && local_expr) {
		// memset to 0
		int         init_size = type_size (def->type);
		int         init_offset = 0;

		if (options.code.progsversion != PROG_ID_VERSION) {
			init_offset = zero_memory (local_expr, def, &type_zero,
									   init_size, init_offset);
		}
		// probably won't happen any time soon, but who knows...
		if (options.code.progsversion != PROG_ID_VERSION
			&& init_size - init_offset >= type_size (&type_quaternion)) {
			init_offset = zero_memory (local_expr, def, &type_quaternion,
									   init_size, init_offset);
		}
		if (init_size - init_offset >= type_size (&type_vector)) {
			init_offset = zero_memory (local_expr, def, &type_vector,
									   init_size, init_offset);
		}
		if (options.code.progsversion != PROG_ID_VERSION
			&& init_size - init_offset >= type_size (&type_double)) {
			init_offset = zero_memory (local_expr, def, &type_double,
									   init_size, init_offset);
		}
		if (init_size - init_offset >= type_size (type_default)) {
			zero_memory (local_expr, def, type_default,
						 init_size, init_offset);
		}
	}
	// it's a global, so already initialized to 0
}

static void
build_element_chain (element_chain_t *element_chain, type_t *type,
					 expr_t *eles, int base_offset)
{
	expr_t     *e = eles->e.block.head;

	if (is_array (type)) {
		type_t     *array_type = type->t.array.type;
		int         array_size = type->t.array.size;
		int         i;

		for (i = 0; i < array_size; i++) {
			int         offset = base_offset + i * type_size (array_type);
			if (e && e->type == ex_block) {
				build_element_chain (element_chain, array_type, e, offset);
			} else {
				element_t  *element = new_element ();
				element->type = array_type;
				element->offset = offset;
				element->expr = e;	// null will be treated as nil
				append_element (element_chain, element);
			}
			if (e) {
				e = e->next;
			}
		}
	} else if (is_struct (type) || is_vector (type) || is_quaternion (type)) {
		symtab_t   *symtab = type->t.symtab;
		symbol_t   *field;

		for (field = symtab->symbols; field; field = field->next) {
			int         offset = base_offset + field->s.offset;
			if (field->sy_type != sy_var
				|| field->visibility == vis_anonymous) {
				continue;
			}
			if (e && e->type == ex_block) {
				build_element_chain (element_chain, field->type, e, offset);
			} else {
				element_t  *element = new_element ();
				element->type = field->type;
				element->offset = offset;
				element->expr = e;	// null will be treated as nil
				append_element (element_chain, element);
			}
			if (e) {
				e = e->next;
			}
		}
	} else {
		error (eles, "invalid initializer");
	}
	if (e && e->next && options.warnings.initializer) {
		warning (eles, "excessive elements in initializer");
	}
}

static void
init_elements (struct def_s *def, expr_t *eles)
{
	expr_t     *c;
	pr_type_t  *g;
	element_chain_t element_chain;
	element_t  *element;

	if (eles->type == ex_nil) {
		init_elements_nil (def);
		return;
	}

	element_chain.head = 0;
	element_chain.tail = &element_chain.head;
	build_element_chain (&element_chain, def->type, eles, 0);

	if (def->local && local_expr) {
		for (element = element_chain.head; element; element = element->next) {
			int         offset = element->offset;
			type_t     *type = element->type;
			expr_t     *ptr = new_pointer_expr (offset, type, def);

			if (element->expr) {
				c = constant_expr (element->expr);
			} else {
				c = new_nil_expr ();
			}
			if (c->type == ex_nil) {
				c = convert_nil (c, type);
			}
			append_expr (local_expr, assign_expr (unary_expr ('.', ptr), c));
		}
	} else {
		def_t       dummy = *def;
		for (element = element_chain.head; element; element = element->next) {
			if (element->expr) {
				c = constant_expr (element->expr);
			} else {
				c = convert_nil (new_nil_expr (), element->type);
			}
			dummy.offset = def->offset + element->offset;
			g = D_POINTER (pr_type_t, &dummy);
			if (c->type == ex_labelref) {
				reloc_def_op (c->e.labelref.label, &dummy);
				continue;
			} else if (c->type == ex_value) {
				if (c->e.value->lltype == ev_integer
					&& is_float (element->type)) {
					convert_int (c);
				}
				if (get_type (c) != element->type) {
					error (c, "type mismatch in initializer");
					continue;
				}
			} else {
				if (!def->local || !local_expr) {
					error (c, "non-constant initializer");
					continue;
				}
			}
			if (c->type != ex_value) {
				internal_error (c, "bogus expression type in init_elements()");
			}
			if (c->e.value->lltype == ev_string) {
				EMIT_STRING (def->space, g->string_var,
							 c->e.value->v.string_val);
			} else {
				memcpy (g, &c->e.value->v, type_size (get_type (c)) * 4);
			}
		}
	}

	*element_chain.tail = elements_freelist;
	elements_freelist = element_chain.head;
}

static void
init_vector_components (symbol_t *vector_sym, int is_field)
{
	expr_t     *vector_expr;
	int         i;
	static const char *fields[] = { "x", "y", "z" };

	vector_expr = new_symbol_expr (vector_sym);
	for (i = 0; i < 3; i++) {
		expr_t     *expr = 0;
		symbol_t   *sym;
		const char *name;

		name = va ("%s_%s", vector_sym->name, fields[i]);
		sym = symtab_lookup (current_symtab, name);
		if (sym) {
			if (sym->table == current_symtab) {
				if (sym->sy_type != sy_expr) {
					error (0, "%s redefined", name);
					sym = 0;
				} else {
					expr = sym->s.expr;
					if (is_field) {
						if (expr->type != ex_value
							|| expr->e.value->lltype != ev_field) {
							error (0, "%s redefined", name);
							sym = 0;
						} else {
							expr->e.value->v.pointer.def = vector_sym->s.def;
							expr->e.value->v.pointer.val = i;
						}
					}
				}
			} else {
				sym = 0;
			}
		}
		if (!sym)
			sym = new_symbol (name);
		if (!expr) {
			if (is_field) {
				expr = new_field_expr (i, &type_float, vector_sym->s.def);
			} else {
				expr = field_expr (vector_expr,
								   new_symbol_expr (new_symbol (fields[i])));
			}
		}
		sym->sy_type = sy_expr;
		sym->s.expr = expr;
		if (!sym->table)
			symtab_addsymbol (current_symtab, sym);
	}
}

static void
init_field_def (def_t *def, expr_t *init, storage_class_t storage)
{
	type_t     *type = def->type->t.fldptr.type;
	def_t      *field_def;
	symbol_t   *field_sym;
	reloc_t    *relocs = 0;

	if (!init)  {
		field_sym = symtab_lookup (pr.entity_fields, def->name);
		if (!field_sym)
			field_sym = new_symbol_type (def->name, type);
		if (field_sym->s.def && field_sym->s.def->external) {
			//FIXME this really is not the right way
			relocs = field_sym->s.def->relocs;
			free_def (field_sym->s.def);
			field_sym->s.def = 0;
		}
		if (!field_sym->s.def) {
			field_sym->s.def = new_def (def->name, type, pr.entity_data, storage);
			reloc_attach_relocs (relocs, &field_sym->s.def->relocs);
			field_sym->s.def->nosave = 1;
		}
		field_def = field_sym->s.def;
		if (!field_sym->table)
			symtab_addsymbol (pr.entity_fields, field_sym);
		if (storage != sc_extern) {
			D_INT (def) = field_def->offset;
			reloc_def_field (field_def, def);
			def->constant = 1;
			def->nosave = 1;
		}
		// no support for initialized field vector componets (yet?)
		if (type == &type_vector && options.code.vector_components)
			init_vector_components (field_sym, 1);
	} else if (init->type == ex_symbol) {
		symbol_t   *sym = init->e.symbol;
		symbol_t   *field = symtab_lookup (pr.entity_fields, sym->name);
		if (field) {
			expr_t     *new = new_field_expr (0, field->type, field->s.def);
			init->type = new->type;
			init->e = new->e;
		}
	}
}

static int
num_elements (expr_t *e)
{
	int         count = 0;
	for (e = e->e.block.head; e; e = e->next) {
		count++;
	}
	return count;
}

void
initialize_def (symbol_t *sym, expr_t *init, defspace_t *space,
				storage_class_t storage)
{
	symbol_t   *check = symtab_lookup (current_symtab, sym->name);
	reloc_t    *relocs = 0;

	if (check && check->table == current_symtab) {
		if (check->sy_type != sy_var || check->type != sym->type) {
			error (0, "%s redefined", sym->name);
		} else {
			// is var and same type
			if (!check->s.def)
				internal_error (0, "half defined var");
			if (storage == sc_extern) {
				if (init)
					error (0, "initializing external variable");
				return;
			}
			if (init && check->s.def->initialized) {
				error (0, "%s redefined", sym->name);
				return;
			}
			sym = check;
		}
	}
	sym->sy_type = sy_var;
	if (!sym->table)
		symtab_addsymbol (current_symtab, sym);

	if (sym->s.def && sym->s.def->external) {
		//FIXME this really is not the right way
		relocs = sym->s.def->relocs;
		free_def (sym->s.def);
		sym->s.def = 0;
	}
	if (!sym->s.def) {
		if (is_array (sym->type) && !type_size (sym->type)
			&& init->type == ex_block && !init->e.block.result) {
			sym->type = array_type (sym->type->t.array.type,
									num_elements (init));
		}
		sym->s.def = new_def (sym->name, sym->type, space, storage);
		reloc_attach_relocs (relocs, &sym->s.def->relocs);
	}
	if (sym->type == &type_vector && options.code.vector_components)
		init_vector_components (sym, 0);
	if (sym->type->type == ev_field && storage != sc_local
		&& storage != sc_param)
		init_field_def (sym->s.def, init, storage);
	if (storage == sc_extern) {
		if (init)
			error (0, "initializing external variable");
		return;
	}
	if (!init)
		return;
	convert_name (init);
	if (init->type == ex_error)
		return;
	if ((is_array (sym->type) || is_struct (sym->type)
		 || sym->type == &type_vector || sym->type == &type_quaternion)
		&& ((init->type == ex_block && !init->e.block.result)
			|| init->type == ex_nil)) {
		init_elements (sym->s.def, init);
		sym->s.def->initialized = 1;
	} else {
		type_t     *init_type;
		if (init->type == ex_nil) {
			convert_nil (init, sym->type);
		}
		init_type = get_type (init);
		if (!type_assignable (sym->type, init_type)) {
			error (init, "type mismatch in initializer");
			return;
		}
		if (local_expr) {
			sym->s.def->initialized = 1;
			init = assign_expr (new_symbol_expr (sym), init);
			// fold_constants takes care of int/float conversions
			append_expr (local_expr, fold_constants (init));
		} else {
			int         offset = 0;
			if (!is_constant (init)) {
				error (init, "non-constant initializier");
				return;
			}
			while ((init->type == ex_uexpr || init->type == ex_expr)
				   && init->e.expr.op == 'A') {
				if (init->type == ex_expr) {
					offset += expr_integer (init->e.expr.e2);
				}
				init = init->e.expr.e1;
			}
			if (init->type != ex_value) {	//FIXME enum etc
				internal_error (0, "initializier not a value");
				return;
			}
			if (init->e.value->lltype == ev_pointer
				|| init->e.value->lltype == ev_field) {
				// FIXME offset pointers
				D_INT (sym->s.def) = init->e.value->v.pointer.val;
				if (init->e.value->v.pointer.def)
					reloc_def_field (init->e.value->v.pointer.def, sym->s.def);
			} else {
				ex_value_t *v = init->e.value;
				if (!init->implicit
					&& is_double (init_type)
					&& (is_integral (sym->type) || is_float (sym->type))) {
					warning (init, "assigning double to %s in initializer "
							 "(use a cast)", sym->type->name);
				}
				if (is_scalar (sym->type))
					v = convert_value (v, sym->type);
				if (v->lltype == ev_string) {
					EMIT_STRING (sym->s.def->space, D_STRING (sym->s.def),
								 v->v.string_val);
				} else {
					memcpy (D_POINTER (void, sym->s.def), &v->v,
							type_size (sym->type) * sizeof (pr_type_t));
				}
			}
			sym->s.def->initialized = 1;
			if (options.code.const_initializers) {
				sym->s.def->constant = 1;
				sym->s.def->nosave = 1;
			}
		}
	}
	sym->s.def->initializer = init;
}

int
def_overlap (def_t *d1, def_t *d2)
{
	int         offs1, size1;
	int         offs2, size2;
	defspace_t *s1 = d1->space;
	defspace_t *s2 = d2->space;

	if (d1->alias)
		s1 = d1->alias->space;
	if (d2->alias)
		s2 = d2->alias->space;
	/// Defs in different spaces never overlap.
	if (s1 != s2)
		return 0;

	offs1 = d1->offset;
	if (d1->alias)
		offs1 += d1->alias->offset;
	size1 = type_size (d1->type);

	offs2 = d2->offset;
	if (d2->alias)
		offs2 += d2->alias->offset;
	size2 = type_size (d2->type);

	if (offs1 <= offs2 && offs1 + size1 >= offs2 + size2)
		return 2;	// d1 fully overlaps d2
	if (offs1 < offs2 + size2 && offs2 < offs1 + size1)
		return 1;	// d1 and d2 at least partially overlap
	return 0;
}

int
def_offset (def_t *def)
{
	int         offset = def->offset;
	if (def->alias)
		offset += def->alias->offset;
	return offset;
}

int
def_size (def_t *def)
{
	return type_size (def->type);
}

int
def_visit_all (def_t *def, int overlap,
			   int (*visit) (def_t *, void *), void *data)
{
	def_t      *start_def = def;
	int         ret;

	if ((ret = visit (def, data)))
		return ret;
	if (def->alias) {
		def = def->alias;
		if ((ret = visit (def, data)))
			return ret;
	}
	for (def = def->alias_defs; def; def = def->next) {
		if (def == start_def)
			continue;
		if (overlap && def_overlap (def, start_def) < overlap)
			continue;
		if ((ret = visit (def, data)))
			return ret;
	}
	return 0;
}
