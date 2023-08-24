/*
	algebra.c

	QC geometric algebra support code

	Copyright (C) 2023 Bill Currie <bill@taniwha.org>

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

#include <ctype.h>
#include <string.h>

#include "QF/darray.h"
#include "QF/dstring.h"
#include "QF/mathlib.h"
#include "QF/va.h"

#include "tools/qfcc/include/algebra.h"
#include "tools/qfcc/include/diagnostic.h"
#include "tools/qfcc/include/expr.h"
#include "tools/qfcc/include/strpool.h"
#include "tools/qfcc/include/struct.h"
#include "tools/qfcc/include/symtab.h"
#include "tools/qfcc/include/type.h"
#include "tools/qfcc/include/value.h"

static struct DARRAY_TYPE (algebra_t *) algebras = DARRAY_STATIC_INIT (16);

static void
binomial (int *coef, int n)
{
	int         c = 1;
	for (int i = 0; i < n + 1; i++) {
		coef[i] = c;
		c = (c * (n - i)) / (i + 1);
	}
}

static int
count_bits (uint32_t v)
{
	int         c = 0;
	for (; v; c++) {
		v &= v - 1;
	}
	return c;
}

static int
count_flips (uint32_t a, uint32_t b)
{
	int         c = 0;
	a >>= 1;
	while (a) {
		c += count_bits (a & b);
		a >>= 1;
	}
	return c;
}

static int
count_minus (uint32_t minus)
{
	return count_bits (minus) & 1 ? -1 : 1;
}
#if 0
static struct_def_t mvec_1d_struct[] = {
	{"vec", &type_vec2},
	{}
};

static struct_def_t mvec_2d_struct[] = {
	{"vec", &type_vec4},
	{}
};

static struct_def_t mvec_3d_struct[] = {
	{"vec",   &type_vec4},
	{"bvec",  &type_vec4},
	{}
};

static struct_def_t mvec_4d_struct[] = {
	{"vec",   &type_vec4},
	{"bvecv", &type_vec4},
	{"bvecm", &type_vec4},
	{"tvec",  &type_vec4},
	{}
};

static struct_def_t *mvec_struct[] = {
	[1] = mvec_1d_struct,
	[2] = mvec_2d_struct,
	[3] = mvec_3d_struct,
	[4] = mvec_4d_struct,
};

static symbol_t *
build_algebra_types (algebra_t *a)
{
	auto name = save_string (va (0, "multivector.%s.%d.%d.%d",
								 a->type->encoding,
								 a->plus, a->minus, a->zero));
	int  dim = a->plus + a->minus + a->zero;
	symbol_t *mvsym;
	if (dim > 4) {
		auto mvec = new_symtab (0, stab_struct);
		int counts[dim + 1];
		binomial (counts, dim);
		auto sym = new_symbol ("scalar");
		sym->type = a->type;
		sym->sy_type = sy_var;
		sym->visibility = vis_public;
		symtab_addsymbol (mvec, sym);
		// skip 0 because the scalar doesn't need a special type
		for (int i = 1; i < dim + 1; i++) {
			sym = new_symbol (va (0, "vec_%d", i));
			sym->type = array_type (a->type, counts[i]);
			sym->sy_type = sy_var;
			sym->visibility = vis_public;
			symtab_addsymbol (mvec, sym);
		}
		mvsym = build_struct ('s', new_symbol (name), mvec, 0, 0);
		if (mvsym->type->alignment < 4) {
			mvsym->type->alignment = 4;
		}
	} else if (dim > 0) {
		mvsym = make_structure (name, 's', mvec_struct[dim], 0);
	} else {
		internal_error (0, "invalid number of dimensions");
	}
	return mvsym;
}
#endif
static void
basis_blade_init (basis_blade_t *blade, pr_uint_t mask)
{
	*blade = (basis_blade_t) {
		.mask = mask,
		.scale = 1,
	};
}

static void
basis_group_init (basis_group_t *group, int count, basis_blade_t *blades,
				  algebra_t *a, int group_id)
{
	pr_uint_t group_mask = 1 << group_id;
	*group = (basis_group_t) {
		.count = count,
		.group_mask = group_mask,
		.range = { ~0u, 0 },
		.blades = malloc (sizeof (basis_blade_t[count])),
		.set = set_new (),
	};
	memcpy (group->blades, blades, sizeof (basis_blade_t[count]));
	for (int i = 0; i < count; i++) {
		pr_uint_t m = blades[i].mask;
		group->range[0] = min (m, group->range[0]);
		group->range[1] = max (m, group->range[1]);
		set_add (group->set, m);
	}
	int         num = group->range[1] - group->range[0] + 1;
	group->map = malloc (sizeof (int[num]));
	for (int i = 0; i < count; i++) {
		group->map[blades[i].mask - group->range[0]] = i;
	}
}

static void
basis_layout_init (basis_layout_t *layout, int count, basis_group_t *groups)
{
	*layout = (basis_layout_t) {
		.count = count,
		.range = { ~0u, 0 },
		.groups = groups,
		.set = set_new (),
	};
	int group_base[count + 1];
	group_base[0] = 0;
	int num_blades = 0;
	for (int i = 0; i < count; i++) {
		set_union (layout->set, groups[i].set);
		group_base[i + 1] = group_base[i] + groups[i].count;
		num_blades += groups[i].count;
		layout->range[0] = min (groups[i].range[0], layout->range[0]);
		layout->range[1] = max (groups[i].range[1], layout->range[1]);
	}
	layout->blade_count = num_blades;
	layout->group_map = malloc (sizeof (pr_ivec3_t[num_blades]));

	int         num = layout->range[1] - layout->range[0] + 1;
	layout->mask_map = calloc (1, sizeof (int[num]));
	int group_inds[count + 1] = {};
	for (int i = 0; i < count; i++) {
		auto g = &groups[i];
		group_inds[i] = 0;
		for (int j = 0; j < g->count; j++) {
			auto b = g->blades[j];
			layout->mask_map[b.mask - layout->range[0]] = group_inds[count];
			layout->group_map[group_inds[count]][0] = i;
			layout->group_map[group_inds[count]][1] = group_inds[i]++;
			layout->group_map[group_inds[count]][2] = group_base[i];
			group_inds[count]++;
		}
	}
}

static void
metric_init (metric_t *metric, int p, int m, int z)
{
	metric->plus = ((1 << p) - 1) << z;
	metric->minus = ((1 << m) - 1) << (z + p);
	metric->zero = (1 << z) - 1;
}

int
metric_apply (const metric_t *metric, pr_uint_t a, pr_uint_t b)
{
	// find all the squared elements
	pr_uint_t c = a & b;
	// any elements that square to 0 result in 0
	if (c & metric->zero) {
		return 0;
	}
	return count_minus (c & metric->minus);
}

static type_t **
alloc_mvec_types (int num_groups)
{
	return calloc (1 << num_groups, sizeof (type_t *));
}

static void
algebra_init (algebra_t *a)
{
	int p = a->plus;
	int m = a->minus;
	int z = a->zero;
	int d = p + m + z;
	metric_init (&a->metric, p, m, z);
	a->dimension = d;
	a->num_components = 1 << d;

	basis_blade_t blades[a->num_components];
	int indices[d + 1];
	int counts[d + 1];
	binomial (counts, d);

	indices[0] = 0;
	for (int i = 0; i < d; i++) {
		indices[i + 1] = indices[i] + counts[i];
	}

	for (int i = 0; i < a->num_components; i++) {
		int         grade = count_bits (i);
		int         ind = indices[grade]++;
		pr_uint_t   mask = i;
		basis_blade_init (&blades[ind], mask);
	}

	if (p == 3 && m == 0 && z == 1) {
		// 3d PGA (w squares to 0, x y z square to +1):
		// : x   y   z   w
		// : yz  zx  xy  1
		// : wx  wy  wz  wxyz
		// : wzy wxz wyx xyz
		basis_blade_t pga_blades[16] = {
			blades[2],  blades[3],  blades[4],  blades[1],
			blades[10], blades[9],  blades[7],  blades[0],
			blades[5],  blades[6],  blades[8],  blades[15],
			blades[13], blades[12], blades[11], blades[14],
		};
		a->groups = malloc (sizeof (basis_group_t[6]));
		a->mvec_types = alloc_mvec_types (6);
		basis_group_init (&a->groups[0], 4, pga_blades +  0, a, 0);
		basis_group_init (&a->groups[1], 3, pga_blades +  4, a, 1);
		basis_group_init (&a->groups[2], 1, pga_blades +  7, a, 2);
		basis_group_init (&a->groups[3], 3, pga_blades +  8, a, 3);
		basis_group_init (&a->groups[4], 1, pga_blades + 11, a, 4);
		basis_group_init (&a->groups[5], 4, pga_blades + 12, a, 5);
		basis_layout_init (&a->layout, 6, a->groups);
	} else if (p == 2 && m == 0 && z == 1) {
		// 2d PGA (w squares to 0, x y square to +1):
		// : yw  xw  xy  1
		// : x   y   w   wxy
		basis_blade_t pga_blades[8] = {
			blades[5], blades[4], blades[6], blades[0],
			blades[2], blades[3], blades[1], blades[7],
		};
		a->groups = malloc (sizeof (basis_group_t[4]));
		a->mvec_types = alloc_mvec_types (4);
		basis_group_init (&a->groups[0], 3, pga_blades + 0, a, 0);
		basis_group_init (&a->groups[1], 1, pga_blades + 3, a, 1);
		basis_group_init (&a->groups[2], 3, pga_blades + 4, a, 2);
		basis_group_init (&a->groups[3], 1, pga_blades + 7, a, 3);
		basis_layout_init (&a->layout, 4, a->groups);
	} else {
		// just use the grades as the default layout
		a->groups = malloc (sizeof (basis_group_t[d + 1]));
		a->mvec_types = alloc_mvec_types (d + 1);
		for (int i = 0; i < d + 1; i++) {
			int         c = counts[i];
			int         ind = indices[i];
			basis_group_init (&a->groups[i], c, &blades[ind - c], a, i);
		}
		basis_layout_init (&a->layout, d + 1, a->groups);
	}

	for (int i = 0; i < a->layout.count; i++) {
		auto g = &a->layout.groups[i];
		if (g->count == 1 && g->blades[0].mask == 0) {
			a->mvec_types[g->group_mask] = a->type;
		} else {
			algebra_mvec_type (a, g->group_mask);
		}
	}
}

bool
is_algebra (const type_t *type)
{
	type = unalias_type (type);
	return type->meta == ty_algebra;
}

type_t *
algebra_type (type_t *type, expr_t *params)
{
	if (!is_float (type) && !is_double (type)) {
		error (0, "algebra type must be float or double");
		return type_default;
	}
	params = reverse_expr_list (params);
	auto plus = params;
	auto minus = plus ? plus->next : 0;
	auto zero = minus ? minus->next : 0;

	expr_t *err = 0;
	if ((plus && !is_integral_val (err = plus))
		|| (minus && !is_integral_val (err = minus))
		|| (zero && !is_integral_val (err = zero))) {
		error (err, "signature must be integral constant");
		return type_default;
	}

	algebra_t search_algebra = {
		.type = type,
		// default to 3,0,1 (plane-based PGA)
		.plus = plus ? expr_integral (plus) : 3,
		.minus = minus ? expr_integral (minus) : 0,
		.zero = zero ? expr_integral (zero) : plus ? 0 : 1,
	};
	int dim = search_algebra.plus + search_algebra.minus + search_algebra.zero;
	if (search_algebra.plus < 0
		|| search_algebra.minus < 0
		|| search_algebra.zero < 0
		|| dim < 1) {
		error (err, "signature must be positive");
		return type_default;
	}
	if (dim > 16) {
		error (err, "signature too large (that's %zd components!)",
			   ((size_t) 1) << dim);
		return type_default;
	}
	algebra_t *algebra = 0;
	for (size_t i = 0; i < algebras.size; i++) {
		auto a = algebras.a[i];
		if (a->type == search_algebra.type
			&& a->plus == search_algebra.plus
			&& a->minus == search_algebra.minus
			&& a->zero == search_algebra.zero) {
			algebra = a;
			break;
		}
	}
	if (!algebra) {
		algebra = malloc (sizeof (algebra_t));
		*algebra = search_algebra;
		DARRAY_APPEND (&algebras, algebra);
		algebra_init (algebra);
	}
	auto t = new_type ();
	t->meta = ty_algebra;
	t->type = ev_invalid;
	t->alignment = (dim > 1 ? 4 : 2) * type->alignment;
	t->t.algebra = algebra;
	algebra->algebra_type = t;
	return find_type (t);
}

type_t *
algebra_mvec_type (algebra_t *algebra, pr_uint_t group_mask)
{
	if (!group_mask) {
		return 0;
	}
	if (!algebra->mvec_types[group_mask]) {
		int count = 0;
		for (int i = 0; i < algebra->layout.count; i++) {
			if (group_mask & (1 << i)) {
				count += algebra->layout.groups[i].count;
			}
		}
		multivector_t *mvec = malloc (sizeof (multivector_t));
		*mvec = (multivector_t) {
			.num_components = count,
			.group_mask = group_mask,
			.algebra = algebra,
		};
		algebra->mvec_types[group_mask] = new_type ();
		*algebra->mvec_types[group_mask] = (type_t) {
			.type = algebra->type->type,
			.name = "basis group",
			.alignment = 4, //FIXME
			.width = count,
			.meta = ty_algebra,
			.t.algebra = (algebra_t *) mvec,
			.freeable = true,
			.allocated = true,
		};
		chain_type (algebra->mvec_types[group_mask]);
	}
	return algebra->mvec_types[group_mask];
}

static int pga_swaps_2d[8] = {
	[0x5] = 1,	// e20
};
static int pga_swaps_3d[16] = {
	[0x7] = 1,	// e021
	[0xa] = 1,	// e31
	[0xd] = 1,	// e032
};

ex_value_t *
algebra_blade_value (algebra_t *alg, const char *name)
{
	uint32_t dimension = alg->plus + alg->minus + alg->zero;
	bool pga_2d = (alg->plus == 2 && alg->minus == 0 && alg->zero == 1);
	bool pga_3d = (alg->plus == 3 && alg->minus == 0 && alg->zero == 1);

	//FIXME supports only 0-9 (ie, up to 10d)
	if (name[0] == 'e' && isdigit(name[1])) {
		int ind = 1;
		while (name[ind] && isdigit ((byte) name[ind])) {
			ind++;
		}
		if (name[ind]) {
			// not a valid basis blade name
			return 0;
		}
		char indices[ind--];
		strcpy (indices, name + 1);
		int swaps = 0;
		uint32_t blade = 0;
		for (int i = 0; i < ind; i++) {
			uint32_t c = indices[i] - '0';
			c -= alg->zero != 1;
			if (c >= dimension) {
				error (0, "basis %c not in algebra %d", indices[i], c);
				continue;
			}
			uint32_t mask = 1u << c;
			if (blade & mask) {
				warning (0, "duplicate index in basis blade");
			}
			swaps += count_flips (blade, mask);
			blade |= mask;
		}
		if (pga_2d) {
			swaps += pga_swaps_2d[blade];
		}
		if (pga_3d) {
			swaps += pga_swaps_3d[blade];
		}
		int sign = 1 - 2 * (swaps & 1);
		auto g = alg->layout.group_map[alg->layout.mask_map[blade]];
		auto group = &alg->layout.groups[g[0]];
		auto group_type = alg->mvec_types[group->group_mask];
		ex_value_t *blade_val = 0;
		if (is_float (alg->type)) {
			float components[group->count] = {};
			components[g[1]] = sign;
			blade_val = new_type_value (group_type, (pr_type_t *)components);
		} else {
			double components[group->count] = {};
			components[g[1]] = sign;
			blade_val = new_type_value (group_type, (pr_type_t *)components);
		}
		return blade_val;
	}
	return 0;
}

static symbol_t *
algebra_symbol (const char *name, symtab_t *symtab)
{
	algebra_t *alg = symtab->procsymbol_data;
	symbol_t *sym = 0;

	auto blade_val = algebra_blade_value (alg, name);
	if (blade_val) {
		sym = new_symbol_type (name, blade_val->type);
		sym->sy_type = sy_const;
		sym->s.value = blade_val;
		symtab_addsymbol (symtab, sym);
	}
	return sym;
}

symtab_t *
algebra_scope (type_t *type, symtab_t *curscope)
{
	auto scope = new_symtab (curscope, stab_local);
	scope->space = curscope->space;

	if (!is_algebra (type)) {
		error (0, "algebra type required for algebra scope");
		return scope;
	}
	scope->procsymbol = algebra_symbol;
	scope->procsymbol_data = unalias_type (type)->t.algebra;
	return scope;
}

algebra_t *
algebra_get (const type_t *type)
{
	if (type->type == ev_invalid) {
		return type->t.algebra;
	} else {
		return type->t.multivec->algebra;
	}
}

void
algebra_print_type_str (dstring_t *str, const type_t *type)
{
	if (type->type == ev_invalid) {
		auto a = type->t.algebra;
		dasprintf (str, " algebra(%s(%d,%d,%d))", a->type->name,
				   a->plus, a->minus, a->zero);
	} else if (type->type == ev_float || type->type == ev_double) {
		auto m = type->t.multivec;
		auto a = m->algebra;
		dasprintf (str, " algebra(%s(%d,%d,%d):%04x)", a->type->name,
				   a->plus, a->minus, a->zero, m->group_mask);
	} else {
		internal_error (0, "invalid algebra type");
	}
}

void
algebra_encode_type (dstring_t *encoding, const type_t *type)
{
	if (type->type == ev_invalid) {
		auto a = type->t.algebra;
		dasprintf (encoding, "{∧");
		encode_type (encoding, a->type);
		dasprintf (encoding, "(%d,%d,%d)}", a->plus, a->minus, a->zero);
	} else if (type->type == ev_float || type->type == ev_double) {
		auto m = type->t.multivec;
		auto a = m->algebra;
		dasprintf (encoding, "{∧");
		encode_type (encoding, a->type);
		dasprintf (encoding, "(%d,%d,%d):%04x}", a->plus, a->minus, a->zero,
				   m->group_mask);
	} else {
		internal_error (0, "invalid algebra type");
	}
}

int
algebra_type_size (const type_t *type)
{
	if (type->type == ev_invalid) {
		auto a = type->t.algebra;
		return a->num_components * type_size (a->type);
	} else if (type->type == ev_float || type->type == ev_double) {
		auto m = type->t.multivec;
		return m->num_components * type_size (m->algebra->type);
	} else {
		internal_error (0, "invalid algebra type");
	}
}

int
algebra_type_width (const type_t *type)
{
	if (type->type == ev_invalid) {
		return 0;
	} else if (type->type == ev_float || type->type == ev_double) {
		auto m = type->t.multivec;
		return m->num_components;
	} else {
		internal_error (0, "invalid algebra type");
	}
}

int
algebra_type_assignable (const type_t *dst, const type_t *src)
{
	if (src->meta == ty_algebra && src->type == ev_invalid) {
		// full algebra types cannot be assigned to anything but the same
		// full algebra type (type represents a full multivector, so the two
		// types are fundametally different), and cannot be assigned to
		// elements of even the same algebra (to get here, the two types
		// had to be different)
		return 0;
	}
	if (dst->meta == ty_algebra && dst->type == ev_invalid) {
		if (is_scalar (src)) {
			// scalars can always be assigned to a full algebra type (sets
			// the scalar element and zeros the other elements)
			return 1;
		}
		if (src->meta != ty_algebra) {
			return 0;
		}
		if (src->t.multivec->algebra != dst->t.algebra) {
			return 0;
		}
		// the multivec is a member of the destination algebra
		return 1;
	}
	if (dst->meta != ty_algebra || src->meta != ty_algebra) {
		return 0;
	}
	return dst->t.multivec == src->t.multivec;
}

type_t *
algebra_base_type (const type_t *type)
{
	if (type->type == ev_invalid) {
		return type->t.algebra->type;
	}
	return ev_types[type->type];
}