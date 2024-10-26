/*
	exprtype.c

	Expression type manipulation

	Copyright (C) 2013 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2013/06/27

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

#include "tools/qfcc/include/algebra.h"
#include "tools/qfcc/include/diagnostic.h"
#include "tools/qfcc/include/expr.h"
#include "tools/qfcc/include/options.h"
#include "tools/qfcc/include/rua-lang.h"
#include "tools/qfcc/include/type.h"

typedef struct {
	int         op;
	const type_t *result_type;
	const type_t *a_cast;
	const type_t *b_cast;
	const expr_t *(*process)(int op, const expr_t *e1, const expr_t *e2);
	bool      (*commutative) (void);
	bool      (*anticommute) (void);
	bool      (*associative) (void);
	int         true_op;
} expr_type_t;

static const expr_t *pointer_arithmetic (int op, const expr_t *e1,
										 const expr_t *e2);
static const expr_t *pointer_compare (int op, const expr_t *e1,
									  const expr_t *e2);
static const expr_t *func_compare (int op, const expr_t *e1,
								   const expr_t *e2);
static const expr_t *inverse_multiply (int op, const expr_t *e1,
									   const expr_t *e2);
static const expr_t *double_compare (int op, const expr_t *e1,
									 const expr_t *e2);
static const expr_t *uint_compare (int op, const expr_t *e1, const expr_t *e2);
static const expr_t *vector_compare (int op, const expr_t *e1, const expr_t *e2);
static const expr_t *quat_compare (int op, const expr_t *e1, const expr_t *e2);
static const expr_t *vector_dot (int op, const expr_t *e1, const expr_t *e2);
static const expr_t *vector_multiply (int op, const expr_t *e1, const expr_t *e2);
static const expr_t *vector_scale (int op, const expr_t *e1, const expr_t *e2);
static const expr_t *entity_compare (int op, const expr_t *e1, const expr_t *e2);

static bool always (void)
{
	return true;
}

static bool fp_com_add (void)
{
	return options.code.commute_float_add;
}

static bool fp_com_mul (void)
{
	return options.code.commute_float_mul;
}

static bool fp_com_dot (void)
{
	return options.code.commute_float_dot;
}

static bool fp_anti_cross (void)
{
	return options.code.anticom_float_cross;
}

static bool fp_anti_sub (void)
{
	return options.code.anticom_float_sub;
}

static bool fp_ass_add (void)
{
	return options.code.assoc_float_add;
}

static bool fp_ass_mul (void)
{
	return options.code.assoc_float_mul;
}

static expr_type_t string_string[] = {
	{'+',	&type_string},
	{QC_EQ,	&type_int},
	{QC_NE,	&type_int},
	{QC_LE,	&type_int},
	{QC_GE,	&type_int},
	{QC_LT,	&type_int},
	{QC_GT,	&type_int},
	{0, 0}
};

static expr_type_t float_float[] = {
	{'+',	&type_float,
		.commutative = fp_com_add, .associative = fp_ass_add},
	{'-',	&type_float,
		.anticommute = fp_anti_sub},
	{'*',	&type_float,
		.commutative = fp_com_mul, .associative = fp_ass_mul},
	{'/',	&type_float},
	{'&',	&type_float,
		.commutative = always, .associative = always},
	{'|',	&type_float,
		.commutative = always, .associative = always},
	{'^',	&type_float,
		.commutative = always, .associative = always},
	{'%',	&type_float},
	{QC_MOD,	&type_float},
	{QC_SHL,	&type_float},
	{QC_SHR,	&type_float},
	{QC_AND,	&type_int},
	{QC_OR,		&type_int},
	{QC_EQ,		&type_int},
	{QC_NE,		&type_int},
	{QC_LE,		&type_int},
	{QC_GE,		&type_int},
	{QC_LT,		&type_int},
	{QC_GT,		&type_int},
	{0, 0}
};

static expr_type_t float_vector[] = {
	{'*',	.process = vector_scale },
	{0, 0}
};

static expr_type_t float_quat[] = {
	{'*',	&type_quaternion},
	{0, 0}
};

static expr_type_t float_int[] = {
	{'+',	&type_float, 0, &type_float,
		.commutative = fp_com_add, .associative = fp_ass_add},
	{'-',	&type_float, 0, &type_float,
		.anticommute = fp_anti_sub},
	{'*',	&type_float, 0, &type_float,
		.commutative = fp_com_mul, .associative = fp_ass_mul},
	{'/',	&type_float, 0, &type_float},
	{'&',	&type_float, 0, &type_float,
		.commutative = always, .associative = always},
	{'|',	&type_float, 0, &type_float,
		.commutative = always, .associative = always},
	{'^',	&type_float, 0, &type_float,
		.commutative = always, .associative = always},
	{'%',	&type_float, 0, &type_float},
	{QC_MOD,	&type_float, 0, &type_float},
	{QC_SHL,	&type_float, 0, &type_float},
	{QC_SHR,	&type_float, 0, &type_float},
	{QC_EQ,		&type_int, 0, &type_float},
	{QC_NE,		&type_int, 0, &type_float},
	{QC_LE,		&type_int, 0, &type_float},
	{QC_GE,		&type_int, 0, &type_float},
	{QC_LT,		&type_int, 0, &type_float},
	{QC_GT,		&type_int, 0, &type_float},
	{0, 0}
};
#define float_uint float_int
#define float_short float_int

static expr_type_t float_double[] = {
	{'+',	&type_double, &type_double, 0,
		.commutative = fp_com_add, .associative = fp_ass_add},
	{'-',	&type_double, &type_double, 0,
		.anticommute = fp_anti_sub},
	{'*',	&type_double, &type_double, 0,
		.commutative = fp_com_mul, .associative = fp_ass_mul},
	{'/',	&type_double, &type_double, 0},
	{'%',	&type_double, &type_double, 0},
	{QC_MOD,	&type_double, &type_double, 0},
	{QC_EQ,		.process = double_compare},
	{QC_NE,		.process = double_compare},
	{QC_LE,		.process = double_compare},
	{QC_GE,		.process = double_compare},
	{QC_LT,		.process = double_compare},
	{QC_GT,		.process = double_compare},
	{0, 0}
};

static expr_type_t vector_float[] = {
	{'*',	.process = vector_scale },
	{'/',	.process = inverse_multiply},
	{0, 0}
};

static expr_type_t vector_vector[] = {
	{'+',	&type_vector,
		.commutative = fp_com_add, .associative = fp_ass_add},
	{'-',	&type_vector,
		.anticommute = fp_anti_sub},
	{QC_DOT,	.process = vector_dot,
		.commutative = fp_com_dot},
	{QC_CROSS,	&type_vector,
		.anticommute = fp_anti_cross},
	{QC_HADAMARD,	&type_vector,
		.commutative = fp_com_mul, .associative = fp_ass_mul},
	{'*',	.process = vector_multiply},
	{QC_EQ,	.process = vector_compare},
	{QC_NE,	.process = vector_compare},
	{0, 0}
};

#define vector_int vector_float
#define vector_uint vector_float
#define vector_short vector_float

static expr_type_t vector_double[] = {
	{'*',	&type_vector, 0, &type_float, vector_scale},
	{'/',	.process = inverse_multiply},
	{0, 0}
};

static expr_type_t entity_entity[] = {
	{QC_EQ,	&type_int, 0, 0, entity_compare},
	{QC_NE,	&type_int, 0, 0, entity_compare},
	{0, 0}
};

static expr_type_t field_field[] = {
	{QC_EQ,	&type_int},
	{QC_NE,	&type_int},
	{0, 0}
};

static expr_type_t func_func[] = {
	{QC_EQ,	.process = func_compare},
	{QC_NE,	.process = func_compare},
	{0, 0}
};

static expr_type_t pointer_pointer[] = {
	{'-',	.process = pointer_arithmetic},
	{QC_EQ,	.process = pointer_compare},
	{QC_NE,	.process = pointer_compare},
	{QC_LE,	.process = pointer_compare},
	{QC_GE,	.process = pointer_compare},
	{QC_LT,	.process = pointer_compare},
	{QC_GT,	.process = pointer_compare},
	{0, 0}
};

static expr_type_t pointer_int[] = {
	{'+',	.process = pointer_arithmetic},
	{'-',	.process = pointer_arithmetic},
	{0, 0}
};
#define pointer_uint pointer_int
#define pointer_short pointer_int

static expr_type_t quat_float[] = {
	{'*',	&type_quaternion},
	{'/',	.process = inverse_multiply},
	{0, 0}
};

static expr_type_t quat_vector[] = {
	{'*',	&type_vector, .true_op = QC_QVMUL},
	{0, 0}
};

static expr_type_t vector_quat[] = {
	{'*',	&type_vector, .true_op = QC_VQMUL},
	{0, 0}
};

static expr_type_t quat_quat[] = {
	{'+',	&type_quaternion,
		.commutative = fp_com_add, .associative = fp_ass_add},
	{'-',	&type_quaternion,
		.anticommute = fp_anti_sub},
	{'*',	&type_quaternion, .associative = always, .true_op = QC_QMUL},
	{QC_EQ,	.process = quat_compare},
	{QC_NE,	.process = quat_compare},
	{0, 0}
};

static expr_type_t quat_int[] = {
	{'*',	&type_quaternion, 0, &type_float},
	{'/',	.process = inverse_multiply},
	{0, 0}
};
#define quat_uint quat_int
#define quat_short quat_int

static expr_type_t quat_double[] = {
	{'*',	&type_quaternion},
	{'/',	.process = inverse_multiply},
	{0, 0}
};

static expr_type_t int_float[] = {
	{'+',	&type_float, &type_float, 0,
		.commutative = fp_com_add, .associative = fp_ass_add},
	{'-',	&type_float, &type_float, 0,
		.anticommute = fp_anti_sub},
	{'*',	&type_float, &type_float, 0,
		.commutative = fp_com_mul, .associative = fp_ass_mul},
	{'/',	&type_float, &type_float, 0},
	{'&',	&type_float, &type_float, 0,
		.commutative = always, .associative = always},
	{'|',	&type_float, &type_float, 0,
		.commutative = always, .associative = always},
	{'^',	&type_float, &type_float, 0,
		.commutative = always, .associative = always},
	{'%',	&type_float, &type_float, 0},
	{QC_MOD,	&type_float, &type_float, 0},
	{QC_SHL,	&type_int, 0, &type_int},	//FIXME?
	{QC_SHR,	&type_int, 0, &type_int},	//FIXME?
	{QC_EQ,		&type_int, &type_float, 0},
	{QC_NE,		&type_int, &type_float, 0},
	{QC_LE,		&type_int, &type_float, 0},
	{QC_GE,		&type_int, &type_float, 0},
	{QC_LT,		&type_int, &type_float, 0},
	{QC_GT,		&type_int, &type_float, 0},
	{0, 0}
};

static expr_type_t int_vector[] = {
	{'*',	&type_vector, &type_float, 0, vector_scale},
	{0, 0}
};

static expr_type_t int_pointer[] = {
	{'+',	.process = pointer_arithmetic},
	{0, 0}
};

static expr_type_t int_quat[] = {
	{'*',	&type_quaternion, &type_float, 0},
	{0, 0}
};

static expr_type_t int_int[] = {
	{'+',	&type_int,
		.commutative = always, .associative = always},
	{'-',	&type_int,
		.anticommute = always},
	{'*',	&type_int,
		.commutative = always, .associative = always},
	{'/',	&type_int},
	{'&',	&type_int,
		.commutative = always, .associative = always},
	{'|',	&type_int,
		.commutative = always, .associative = always},
	{'^',	&type_int,
		.commutative = always, .associative = always},
	{'%',	&type_int},
	{QC_MOD,	&type_int},
	{QC_SHL,	&type_int},
	{QC_SHR,	&type_int},
	{QC_AND,	&type_int},
	{QC_OR,		&type_int},
	{QC_EQ,		&type_int},
	{QC_NE,		&type_int},
	{QC_LE,		&type_int},
	{QC_GE,		&type_int},
	{QC_LT,		&type_int},
	{QC_GT,		&type_int},
	{0, 0}
};

static expr_type_t int_uint[] = {
	{'+',	&type_int, 0, &type_int,
		.commutative = always, .associative = always},
	{'-',	&type_int, 0, &type_int,
		.anticommute = always},
	{'*',	&type_int, 0, &type_int,
		.commutative = always, .associative = always},
	{'/',	&type_int, 0, &type_int},
	{'&',	&type_int, 0, &type_int,
		.commutative = always, .associative = always},
	{'|',	&type_int, 0, &type_int,
		.commutative = always, .associative = always},
	{'^',	&type_int, 0, &type_int,
		.commutative = always, .associative = always},
	{'%',	&type_int, 0, &type_int},
	{QC_MOD,	&type_int, 0, &type_int},
	{QC_SHL,	&type_int, 0, &type_int},
	{QC_SHR,	&type_int, 0, &type_int},
	{QC_EQ,		&type_int, 0, &type_int},
	{QC_NE,		&type_int, 0, &type_int},
	{QC_LE,		.process = uint_compare},
	{QC_GE,		.process = uint_compare},
	{QC_LT,		.process = uint_compare},
	{QC_GT,		.process = uint_compare},
	{0, 0}
};

static expr_type_t int_short[] = {
	{'+',	&type_int, 0, &type_int,
		.commutative = always, .associative = always},
	{'-',	&type_int, 0, &type_int,
		.anticommute = always},
	{'*',	&type_int, 0, &type_int,
		.commutative = always, .associative = always},
	{'/',	&type_int, 0, &type_int},
	{'&',	&type_int, 0, &type_int,
		.commutative = always, .associative = always},
	{'|',	&type_int, 0, &type_int,
		.commutative = always, .associative = always},
	{'^',	&type_int, 0, &type_int,
		.commutative = always, .associative = always},
	{'%',	&type_int, 0, &type_int},
	{QC_MOD,	&type_int, 0, &type_int},
	{QC_SHL,	&type_int, 0, &type_int},
	{QC_SHR,	&type_int, 0, &type_int},
	{QC_EQ,		&type_int, 0, &type_int},
	{QC_NE,		&type_int, 0, &type_int},
	{QC_LE,		&type_int, 0, &type_int},
	{QC_GE,		&type_int, 0, &type_int},
	{QC_LT,		&type_int, 0, &type_int},
	{QC_GT,		&type_int, 0, &type_int},
	{0, 0}
};

static expr_type_t int_double[] = {
	{'+',	&type_double, &type_double, 0,
		.commutative = fp_com_add, .associative = fp_ass_add},
	{'-',	&type_double, &type_double, 0,
		.anticommute = fp_com_add},
	{'*',	&type_double, &type_double, 0,
		.commutative = fp_com_mul, .associative = fp_ass_mul},
	{'/',	&type_double, &type_double, 0},
	{'%',	&type_double, &type_double, 0},
	{QC_MOD,	&type_double, &type_double, 0},
	{QC_EQ,		&type_long, &type_double, 0},
	{QC_NE,		&type_long, &type_double, 0},
	{QC_LE,		&type_long, &type_double, 0},
	{QC_GE,		&type_long, &type_double, 0},
	{QC_LT,		&type_long, &type_double, 0},
	{QC_GT,		&type_long, &type_double, 0},
	{0, 0}
};

#define uint_float int_float
#define uint_vector int_vector
#define uint_pointer int_pointer
#define uint_quat int_quat

static expr_type_t uint_int[] = {
	{'+',	&type_int, &type_int, &type_int,
		.commutative = always, .associative = always},
	{'-',	&type_int, &type_int, &type_int,
		.anticommute = always },
	{'*',	&type_int, &type_int, &type_int,
		.commutative = always, .associative = always},
	{'/',	&type_int, &type_int, &type_int },
	{'&',	&type_int, &type_int, &type_int,
		.commutative = always, .associative = always},
	{'|',	&type_int, &type_int, &type_int,
		.commutative = always, .associative = always},
	{'^',	&type_int, &type_int, &type_int,
		.commutative = always, .associative = always},
	{'%',	&type_int, &type_int, &type_int },
	{QC_MOD,	&type_int, &type_int, &type_int },
	{QC_SHL,	&type_uint, &type_int, &type_int },
	{QC_SHR,	&type_uint, 0,        &type_int },
	{QC_EQ,		&type_int, &type_int, &type_int },
	{QC_NE,		&type_int, &type_int, &type_int },
	{QC_LE,		.process = uint_compare},
	{QC_GE,		.process = uint_compare},
	{QC_LT,		.process = uint_compare},
	{QC_GT,		.process = uint_compare},
	{0, 0}
};

static expr_type_t uint_uint[] = {
	{'+',	&type_uint,
		.commutative = always, .associative = always},
	{'-',	&type_uint,
		.anticommute = always},
	{'*',	&type_uint,
		.commutative = always, .associative = always},
	{'/',	&type_uint},
	{'&',	&type_uint,
		.commutative = always, .associative = always},
	{'|',	&type_uint,
		.commutative = always, .associative = always},
	{'^',	&type_uint,
		.commutative = always, .associative = always},
	{'%',	&type_uint},
	{QC_MOD,	&type_uint},
	{QC_SHL,	&type_uint},
	{QC_SHR,	&type_uint},
	{QC_EQ,		&type_int, &type_int, &type_int},
	{QC_NE,		&type_int, &type_int, &type_int},
	{QC_LE,		&type_int},
	{QC_GE,		&type_int},
	{QC_LT,		&type_int},
	{QC_GT,		&type_int},
	{0, 0}
};
#define uint_short uint_int
#define uint_double int_double

#define short_float int_float
#define short_vector int_vector
#define short_pointer int_pointer
#define short_quat int_quat

static expr_type_t short_int[] = {
	{'+',	&type_int, &type_int, 0,
		.commutative = always, .associative = always},
	{'-',	&type_int, &type_int, 0,
		.anticommute = always},
	{'*',	&type_int, &type_int, 0,
		.commutative = always, .associative = always},
	{'/',	&type_int, &type_int, 0},
	{'&',	&type_int, &type_int, 0,
		.commutative = always, .associative = always},
	{'|',	&type_int, &type_int, 0,
		.commutative = always, .associative = always},
	{'^',	&type_int, &type_int, 0,
		.commutative = always, .associative = always},
	{'%',	&type_int, &type_int, 0},
	{QC_MOD,	&type_int, &type_int, 0},
	{QC_SHL,	&type_short},
	{QC_SHR,	&type_short},
	{QC_EQ,		&type_int, &type_int, 0},
	{QC_NE,		&type_int, &type_int, 0},
	{QC_LE,		&type_int, &type_int, 0},
	{QC_GE,		&type_int, &type_int, 0},
	{QC_LT,		&type_int, &type_int, 0},
	{QC_GT,		&type_int, &type_int, 0},
	{0, 0}
};

static expr_type_t short_uint[] = {
	{'+',	&type_uint, &type_uint, 0,
		.commutative = always},
	{'-',	&type_uint, &type_uint, 0,
		.anticommute = always},
	{'*',	&type_uint, &type_uint, 0,
		.commutative = always},
	{'/',	&type_uint, &type_uint, 0},
	{'&',	&type_uint, &type_uint, 0,
		.commutative = always},
	{'|',	&type_uint, &type_uint, 0,
		.commutative = always},
	{'^',	&type_uint, &type_uint, 0,
		.commutative = always},
	{'%',	&type_uint, &type_uint, 0},
	{QC_MOD,	&type_uint, &type_uint, 0},
	{QC_SHL,	&type_short},
	{QC_SHR,	&type_short},
	{QC_EQ,		&type_int, &type_uint, 0},
	{QC_NE,		&type_int, &type_uint, 0},
	{QC_LE,		&type_int, &type_uint, 0},
	{QC_GE,		&type_int, &type_uint, 0},
	{QC_LT,		&type_int, &type_uint, 0},
	{QC_GT,		&type_int, &type_uint, 0},
	{0, 0}
};

static expr_type_t short_short[] = {
	{'+',	&type_short},
	{'-',	&type_short},
	{'*',	&type_short},
	{'/',	&type_short},
	{'&',	&type_short},
	{'|',	&type_short},
	{'^',	&type_short},
	{'%',	&type_short},
	{QC_MOD,	&type_short},
	{QC_SHL,	&type_short},
	{QC_SHR,	&type_short},
	{QC_EQ,		&type_int},
	{QC_NE,		&type_int},
	{QC_LE,		&type_int},
	{QC_GE,		&type_int},
	{QC_LT,		&type_int},
	{QC_GT,		&type_int},
	{0, 0}
};
#define short_double int_double

static expr_type_t double_float[] = {
	{'+',	&type_double, 0, &type_double,
		.commutative = fp_com_add, .associative = fp_ass_add},
	{'-',	&type_double, 0, &type_double,
		.anticommute = fp_anti_sub},
	{'*',	&type_double, 0, &type_double,
		.commutative = fp_com_mul, .associative = fp_ass_mul},
	{'/',	&type_double, 0, &type_double},
	{'%',	&type_double, 0, &type_double},
	{QC_MOD,	&type_double, 0, &type_double},
	{QC_EQ,		.process = double_compare},
	{QC_NE,		.process = double_compare},
	{QC_LE,		.process = double_compare},
	{QC_GE,		.process = double_compare},
	{QC_LT,		.process = double_compare},
	{QC_GT,		.process = double_compare},
	{0, 0}
};

static expr_type_t double_vector[] = {
	{'*',	&type_vector, &type_float, 0, vector_scale},
	{0, 0}
};

static expr_type_t double_quat[] = {
	{'*',	&type_quaternion},
	{0, 0}
};

static expr_type_t double_int[] = {
	{'+',	&type_double, 0, &type_double,
		.commutative = fp_com_add, .associative = fp_ass_add},
	{'-',	&type_double, 0, &type_double,
		.anticommute = fp_anti_sub},
	{'*',	&type_double, 0, &type_double,
		.commutative = fp_com_mul, .associative = fp_ass_mul},
	{'/',	&type_double, 0, &type_double},
	{'%',	&type_double, 0, &type_double},
	{QC_MOD,	&type_double, 0, &type_double},
	{QC_EQ,		.process = double_compare},
	{QC_NE,		.process = double_compare},
	{QC_LE,		.process = double_compare},
	{QC_GE,		.process = double_compare},
	{QC_LT,		.process = double_compare},
	{QC_GT,		.process = double_compare},
	{0, 0}
};
#define double_uint double_int
#define double_short double_int

static expr_type_t double_double[] = {
	{'+',	&type_double,
		.commutative = fp_com_add, .associative = fp_ass_add},
	{'-',	&type_double,
		.anticommute = fp_anti_sub},
	{'*',	&type_double,
		.commutative = fp_com_mul, .associative = fp_ass_mul},
	{'/',	&type_double},
	{'%',	&type_double},
	{QC_MOD,	&type_double},
	{QC_EQ,		&type_long},
	{QC_NE,		&type_long},
	{QC_LE,		&type_long},
	{QC_GE,		&type_long},
	{QC_LT,		&type_long},
	{QC_GT,		&type_long},
	{0, 0}
};

static expr_type_t long_long[] = {
	{'+',	&type_long,
		.commutative = always},
	{'-',	&type_long,
		.anticommute = always},
	{'*',	&type_long,
		.commutative = always},
	{'/',	&type_long},
	{'&',	&type_long,
		.commutative = always},
	{'|',	&type_long,
		.commutative = always},
	{'^',	&type_long,
		.commutative = always},
	{'%',	&type_long},
	{QC_MOD,	&type_long},
	{QC_SHL,	&type_long},
	{QC_SHR,	&type_long},
	{QC_EQ,		&type_long},
	{QC_NE,		&type_long},
	{QC_LE,		&type_long},
	{QC_GE,		&type_long},
	{QC_LT,		&type_long},
	{QC_GT,		&type_long},
	{0, 0}
};

static expr_type_t ulong_ulong[] = {
	{'+',	&type_ulong,
		.commutative = always},
	{'-',	&type_ulong,
		.anticommute = always},
	{'*',	&type_ulong,
		.commutative = always},
	{'/',	&type_ulong},
	{'&',	&type_ulong,
		.commutative = always},
	{'|',	&type_ulong,
		.commutative = always},
	{'^',	&type_ulong,
		.commutative = always},
	{'%',	&type_ulong},
	{QC_MOD,	&type_ulong},
	{QC_SHL,	&type_ulong},
	{QC_SHR,	&type_ulong},
	{QC_EQ,		&type_long},
	{QC_NE,		&type_long},
	{QC_LE,		&type_long},
	{QC_GE,		&type_long},
	{QC_LT,		&type_long},
	{QC_GT,		&type_long},
	{0, 0}
};

static expr_type_t *string_x[ev_type_count] = {
	[ev_string] = string_string,
};

static expr_type_t *float_x[ev_type_count] = {
	[ev_float] = float_float,
	[ev_vector] = float_vector,
	[ev_quaternion] = float_quat,
	[ev_int] = float_int,
	[ev_uint] = float_uint,
	[ev_short] = float_short,
	[ev_double] = float_double,
};

static expr_type_t *vector_x[ev_type_count] = {
	[ev_float] = vector_float,
	[ev_vector] = vector_vector,
	[ev_quaternion] = vector_quat,
	[ev_int] = vector_int,
	[ev_uint] = vector_uint,
	[ev_short] = vector_short,
	[ev_double] = vector_double,
};

static expr_type_t *entity_x[ev_type_count] = {
	[ev_entity] = entity_entity,
};

static expr_type_t *field_x[ev_type_count] = {
	[ev_field] = field_field,
};

static expr_type_t *func_x[ev_type_count] = {
	[ev_func] = func_func,
};

static expr_type_t *pointer_x[ev_type_count] = {
	[ev_ptr] = pointer_pointer,
	[ev_int] = pointer_int,
	[ev_uint] = pointer_uint,
	[ev_short] = pointer_short,
};

static expr_type_t *quat_x[ev_type_count] = {
	[ev_float] = quat_float,
	[ev_vector] = quat_vector,
	[ev_quaternion] = quat_quat,
	[ev_int] = quat_int,
	[ev_uint] = quat_uint,
	[ev_short] = quat_short,
	[ev_double] = quat_double,
};

static expr_type_t *int_x[ev_type_count] = {
	[ev_float] = int_float,
	[ev_vector] = int_vector,
	[ev_ptr] = int_pointer,
	[ev_quaternion] = int_quat,
	[ev_int] = int_int,
	[ev_uint] = int_uint,
	[ev_short] = int_short,
	[ev_double] = int_double,
};

static expr_type_t *uint_x[ev_type_count] = {
	[ev_float] = uint_float,
	[ev_vector] = uint_vector,
	[ev_ptr] = uint_pointer,
	[ev_quaternion] = uint_quat,
	[ev_int] = uint_int,
	[ev_uint] = uint_uint,
	[ev_short] = uint_short,
	[ev_double] = uint_double,
};

static expr_type_t *short_x[ev_type_count] = {
	[ev_float] = short_float,
	[ev_vector] = short_vector,
	[ev_ptr] = short_pointer,
	[ev_quaternion] = short_quat,
	[ev_int] = short_int,
	[ev_uint] = short_uint,
	[ev_short] = short_short,
	[ev_double] = short_double,
};

static expr_type_t *double_x[ev_type_count] = {
	[ev_float] = double_float,
	[ev_vector] = double_vector,
	[ev_quaternion] = double_quat,
	[ev_int] = double_int,
	[ev_uint] = double_uint,
	[ev_short] = double_short,
	[ev_double] = double_double,
};

static expr_type_t *long_x[ev_type_count] = {
	[ev_long] = long_long,
};

static expr_type_t *ulong_x[ev_type_count] = {
	[ev_ulong] = ulong_ulong,
};

static expr_type_t **binary_expr_types[ev_type_count] = {
	[ev_string] = string_x,
	[ev_float] = float_x,
	[ev_vector] = vector_x,
	[ev_entity] = entity_x,
	[ev_field] = field_x,
	[ev_func] = func_x,
	[ev_ptr] = pointer_x,
	[ev_quaternion] = quat_x,
	[ev_int] = int_x,
	[ev_uint] = uint_x,
	[ev_short] = short_x,
	[ev_double] = double_x,
	[ev_long] = long_x,
	[ev_ulong] = ulong_x,
};

static expr_type_t int_handle[] = {
	{QC_EQ,	&type_int},
	{QC_NE,	&type_int},

	{0, 0}
};

static expr_type_t long_handle[] = {
	{QC_EQ,	&type_int},
	{QC_NE,	&type_int},

	{0, 0}
};

static expr_type_t *int_handle_x[ev_type_count] = {
	[ev_int] = int_handle,
};

static expr_type_t *long_handle_x[ev_type_count] = {
	[ev_long] = long_handle,
};

static expr_type_t **binary_expr_handle[ev_type_count] = {
	[ev_int] = int_handle_x,
	[ev_long] = long_handle_x,
};

static expr_type_t ***binary_expr_meta[ty_meta_count] = {
	[ty_basic] = binary_expr_types,
	[ty_enum] = binary_expr_types,
	[ty_alias] = binary_expr_types,
	[ty_handle] = binary_expr_handle,
};

// supported operators for scalar-vector expressions
static int scalar_vec_ops[] = { '*', '/', '%', QC_MOD, 0 };
static const expr_t *
convert_scalar (const expr_t *scalar, int op, const expr_t *vec)
{
	int        *s_op = scalar_vec_ops;
	while (*s_op && *s_op != op) {
		s_op++;
	}
	if (!*s_op) {
		return 0;
	}

	// expand the scalar to a vector of the same width as vec
	auto vec_type = get_type (vec);

	if (is_constant (scalar)) {
		int width = type_width (get_type (vec));
		const expr_t *elements[width];
		for (int i = 0; i < width; i++) {
			elements[i] = scalar;
		}
		auto scalar_list = new_list_expr (0);
		list_gather (&scalar_list->list, elements, width);
		return new_vector_list (scalar_list);
	}

	return new_extend_expr (scalar, vec_type, 2, false);//2 = copy
}

static const expr_t *
pointer_arithmetic (int op, const expr_t *e1, const expr_t *e2)
{
	auto t1 = get_type (e1);
	auto t2 = get_type (e2);
	const expr_t *ptr = 0;
	const expr_t *offset = 0;
	const expr_t *psize;
	const type_t *ptype = 0;

	if (!is_pointer (t1) && !is_pointer (t2)) {
		internal_error (e1, "pointer arithmetic on non-pointers");
	}
	if (is_pointer (t1) && is_pointer (t2)) {
		if (op != '-') {
			return error (e2, "invalid pointer operation");
		}
		if (t1 != t2) {
			return error (e2, "cannot use %c on pointers of different types",
						  op);
		}
		e1 = cast_expr (&type_int, e1);
		e2 = cast_expr (&type_int, e2);
		psize = new_int_expr (type_size (t1->fldptr.type), false);
		return binary_expr ('/', binary_expr ('-', e1, e2), psize);
	} else if (is_pointer (t1)) {
		offset = cast_expr (&type_int, e2);
		ptr = e1;
		ptype = t1;
	} else if (is_pointer (t2)) {
		offset = cast_expr (&type_int, e1);
		ptr = e2;
		ptype = t2;
	}
	// op is known to be + or -
	psize = new_int_expr (type_size (ptype->fldptr.type), false);
	offset = unary_expr (op, binary_expr ('*', offset, psize));
	return offset_pointer_expr (ptr, offset);
}

static const expr_t *
pointer_compare (int op, const expr_t *e1, const expr_t *e2)
{
	auto t1 = get_type (e1);
	auto t2 = get_type (e2);
	expr_t     *e;

	if (!type_assignable (t1, t2)) {
		return error (e2, "cannot use %s on pointers of different types",
					  get_op_string (op));
	}
	if (options.code.progsversion < PROG_VERSION) {
		e = new_binary_expr (op, e1, e2);
	} else {
		e = new_binary_expr (op, cast_expr (&type_int, e1),
							 cast_expr (&type_int, e2));
	}
	e->expr.type = &type_int;
	return e;
}

static const expr_t *
func_compare (int op, const expr_t *e1, const expr_t *e2)
{
	expr_t     *e;

	if (options.code.progsversion < PROG_VERSION) {
		e = new_binary_expr (op, e1, e2);
	} else {
		e = new_binary_expr (op, new_alias_expr (&type_int, e1),
							 new_alias_expr (&type_int, e2));
	}
	e->expr.type = &type_int;
	if (options.code.progsversion == PROG_ID_VERSION) {
		e->expr.type = &type_float;
	}
	return e;
}

static const expr_t *
inverse_multiply (int op, const expr_t *e1, const expr_t *e2)
{
	// There is no vector/float or quaternion/float instruction and adding
	// one would mean the engine would have to do 1/f every time
	auto one = new_float_expr (1, false);
	return binary_expr ('*', e1, binary_expr ('/', one, e2));
}

static const expr_t *
vector_compare (int op, const expr_t *e1, const expr_t *e2)
{
	if (options.code.progsversion < PROG_VERSION) {
		expr_t     *e = new_binary_expr (op, e1, e2);
		e->expr.type = &type_int;
		if (options.code.progsversion == PROG_ID_VERSION) {
			e->expr.type = &type_float;
		}
		return e;
	}
	int         hop = op == QC_EQ ? '&' : '|';
	e1 = new_alias_expr (&type_vec3, e1);
	e2 = new_alias_expr (&type_vec3, e2);
	expr_t     *e = new_binary_expr (op, e1, e2);
	e->expr.type = &type_ivec3;
	return new_horizontal_expr (hop, e, &type_int);
}

static const expr_t *
quat_compare (int op, const expr_t *e1, const expr_t *e2)
{
	if (options.code.progsversion < PROG_VERSION) {
		expr_t     *e = new_binary_expr (op, e1, e2);
		e->expr.type = &type_int;
		return e;
	}
	int         hop = op == QC_EQ ? '&' : '|';
	e1 = new_alias_expr (&type_vec4, e1);
	e2 = new_alias_expr (&type_vec4, e2);
	expr_t     *e = new_binary_expr (op, e1, e2);
	e->expr.type = &type_ivec4;
	return new_horizontal_expr (hop, e, &type_int);
}

static const expr_t *
vector_dot (int op, const expr_t *e1, const expr_t *e2)
{
	expr_t     *e = new_binary_expr (QC_DOT, e1, e2);
	e->expr.type = &type_float;
	return e;
}

static const expr_t *
vector_multiply (int op, const expr_t *e1, const expr_t *e2)
{
	if (options.math.vector_mult == QC_DOT) {
		// vector * vector is dot product in v6 progs (ick)
		return vector_dot (op, e1, e2);
	}
	// component-wise multiplication
	expr_t     *e = new_binary_expr ('*', e1, e2);
	e->expr.type = &type_vector;
	return e;
}

static const expr_t *
vector_scale (int op, const expr_t *e1, const expr_t *e2)
{
	// Ensure the expression is always vector * scalar. The operation is
	// always commutative, and the Ruamoko ISA supports only vector * scalar
	// (though v6 does support scalar * vector, one less if).
	if (is_scalar (get_type (e1))) {
		auto t = e1;
		e1 = e2;
		e2 = t;
	}
	expr_t     *e = new_binary_expr (QC_SCALE, e1, e2);
	e->expr.type = get_type (e1);
	return e;
}

static const expr_t *
double_compare (int op, const expr_t *e1, const expr_t *e2)
{
	auto t1 = get_type (e1);
	auto t2 = get_type (e2);
	expr_t     *e;

	if (is_constant (e1) && e1->implicit && is_double (t1) && is_float (t2)) {
		t1 = &type_float;
		e1 = cast_expr (t1, e1);
	}
	if (is_float (t1) && is_constant (e2) && e2->implicit && is_double (t2)) {
		t2 = &type_float;
		e2 = cast_expr (t2, e2);
	}
	if (is_double (t1)) {
		if (is_float (t2)) {
			warning (e2, "comparison between double and float");
		} else if (!is_constant (e2)) {
			warning (e2, "comparison between double and int");
		}
		e2 = cast_expr (&type_double, e2);
	} else if (is_double (t2)) {
		if (is_float (t1)) {
			warning (e1, "comparison between float and double");
		} else if (!is_constant (e1)) {
			warning (e1, "comparison between int and double");
		}
		e1 = cast_expr (&type_double, e1);
	}
	e = new_binary_expr (op, e1, e2);
	e->expr.type = &type_long;
	return e;
}

static const expr_t *
uint_compare (int op, const expr_t *e1, const expr_t *e2)
{
	auto t1 = get_type (e1);
	auto t2 = get_type (e2);
	expr_t     *e;

	if (is_constant (e1) && e1->implicit && is_int (t1)) {
		t1 = &type_uint;
		e1 = cast_expr (t1, e1);
	}
	if (is_constant (e2) && e2->implicit && is_int (t2)) {
		t2 = &type_uint;
		e2 = cast_expr (t2, e2);
	}
	if (t1 != t2) {
		warning (e1, "comparison between signed and unsigned");
		if (is_int (t1)) {
			e1 = cast_expr (&type_uint, e2);
		} else {
			e2 = cast_expr (&type_uint, e2);
		}
	}
	e = new_binary_expr (op, e1, e2);
	e->expr.type = &type_int;
	return e;
}

static const expr_t *
entity_compare (int op, const expr_t *e1, const expr_t *e2)
{
	if (options.code.progsversion == PROG_VERSION) {
		e1 = new_alias_expr (&type_int, e1);
		e2 = new_alias_expr (&type_int, e2);
	}
	expr_t     *e = new_binary_expr (op, e1, e2);
	e->expr.type = &type_int;
	if (options.code.progsversion == PROG_ID_VERSION) {
		e->expr.type = &type_float;
	}
	return e;
}

#define invalid_binary_expr(_op, _e1, _e2) \
	_invalid_binary_expr(_op, _e1, _e2, __FILE__, __LINE__, __FUNCTION__)
static const expr_t *
_invalid_binary_expr (int op, const expr_t *e1, const expr_t *e2,
					  const char *file, int line, const char *func)
{
	auto t1 = get_type (e1);
	auto t2 = get_type (e2);
	return _error (e1, file, line, func, "invalid binary expression: %s %s %s",
				  get_type_string (t1), get_op_string (op),
				  get_type_string (t2));
}

static const expr_t *
reimplement_binary_expr (int op, const expr_t *e1, const expr_t *e2)
{
	expr_t     *e;

	if (options.code.progsversion == PROG_ID_VERSION) {
		switch (op) {
			case '%':
				{
					expr_t     *tmp1, *tmp2;
					e = new_block_expr (0);
					tmp1 = new_temp_def_expr (&type_float);
					tmp2 = new_temp_def_expr (&type_float);

					append_expr (e, assign_expr (tmp1, binary_expr ('/', e1, e2)));
					append_expr (e, assign_expr (tmp2, binary_expr ('&', tmp1, tmp1)));
					e->block.result = binary_expr ('-', e1, binary_expr ('*', e2, tmp2));
					return e;
				}
				break;
		}
	}
	return 0;
}

static const expr_t *
check_precedence (int op, const expr_t *e1, const expr_t *e2)
{
	if (e1->type == ex_uexpr && e1->expr.op == '!' && !e1->paren) {
		if (options.traditional) {
			if (op != QC_AND && op != QC_OR && op != '=') {
				notice (e1, "precedence of `!' and `%s' inverted for "
							"traditional code", get_op_string (op));
				e1 = paren_expr (e1->expr.e1);
				return unary_expr ('!', binary_expr (op, e1, e2));
			}
		} else if (op == '&' || op == '|') {
			if (options.warnings.precedence)
				warning (e1, "ambiguous logic. Suggest explicit parentheses "
						 "with expressions involving ! and %s",
						 get_op_string (op));
		}
	}
	if (options.traditional) {
		if (e2->type == ex_expr && !e2->paren) {
			if (((op == '&' || op == '|')
				 && (is_math_op (e2->expr.op) || is_compare (e2->expr.op)))
				|| (op == '='
					&&(e2->expr.op == QC_OR || e2->expr.op == QC_AND))) {
				notice (e1, "precedence of `%s' and `%s' inverted for "
							"traditional code", get_op_string (op),
							get_op_string (e2->expr.op));
				e1 = binary_expr (op, e1, e2->expr.e1);
				e1 = paren_expr (e1);
				return binary_expr (e2->expr.op, e1, e2->expr.e2);
			}
			if (((op == QC_EQ || op == QC_NE) && is_compare (e2->expr.op))
				|| (op == QC_OR && e2->expr.op == QC_AND)
				|| (op == '|' && e2->expr.op == '&')) {
				notice (e1, "precedence of `%s' raised to `%s' for "
							"traditional code", get_op_string (op),
							get_op_string (e2->expr.op));
				e1 = binary_expr (op, e1, e2->expr.e1);
				e1 = paren_expr (e1);
				return binary_expr (e2->expr.op, e1, e2->expr.e2);
			}
		} else if (e1->type == ex_expr && !e1->paren) {
			if (((op == '&' || op == '|')
				 && (is_math_op (e1->expr.op) || is_compare (e1->expr.op)))
				|| (op == '='
					&&(e2->expr.op == QC_OR || e2->expr.op == QC_AND))) {
				notice (e1, "precedence of `%s' and `%s' inverted for "
							"traditional code", get_op_string (op),
							get_op_string (e1->expr.op));
				e2 = binary_expr (op, e1->expr.e2, e2);
				e1 = paren_expr (e1->expr.e1);
				return binary_expr (e1->expr.op, e1, e2);
			}
		}
	} else {
		if (e2->type == ex_expr && !e2->paren) {
			if ((op == '&' || op == '|' || op == '^')
				&& (is_math_op (e2->expr.op)
					|| is_compare (e2->expr.op))) {
				if (options.warnings.precedence)
					warning (e2, "suggest parentheses around %s in "
							 "operand of %c",
							 is_compare (e2->expr.op)
									? "comparison"
									: get_op_string (e2->expr.op),
							 op);
			}
		}
		if (e1->type == ex_expr && !e1->paren) {
			if ((op == '&' || op == '|' || op == '^')
				&& (is_math_op (e1->expr.op)
					|| is_compare (e1->expr.op))) {
				if (options.warnings.precedence)
					warning (e1, "suggest parentheses around %s in "
							 "operand of %c",
							 is_compare (e1->expr.op)
									? "comparison"
									: get_op_string (e1->expr.op),
							 op);
			}
		}
	}
	return 0;
}

static int
is_call (const expr_t *e)
{
	return e->type == ex_block && e->block.is_call;
}

static const type_t *
promote_type (const type_t *dst, const type_t *src)
{
	if (is_vector (dst) || is_quaternion (dst)) {
		return dst;
	}
	return vector_type (base_type (dst), type_width (src));
}

const expr_t *
binary_expr (int op, const expr_t *e1, const expr_t *e2)
{
	etype_t     et1, et2;
	const expr_t *e;
	expr_type_t *expr_type;

	e1 = convert_name (e1);
	// FIXME this is target-specific info and should not be in the
	// expression tree
	if (e1->type == ex_alias && is_call (e1->alias.expr)) {
		// move the alias expression inside the block so the following check
		// can detect the call and move the temp assignment into the block
		auto block = (expr_t *) e1->alias.expr;
		auto ne = new_expr ();
		*ne = *e1;
		ne->alias.expr = block->block.result;
		block->block.result = ne;
		e1 = block;
	}
	if (e1->type == ex_block && e1->block.is_call
		&& has_function_call (e2) && e1->block.result) {
		// the temp assignment needs to be inside the block so assignment
		// code generation doesn't see it when applying right-associativity
		expr_t    *tmp = new_temp_def_expr (get_type (e1->block.result));
		auto ne = assign_expr (tmp, e1->block.result);
		auto nb = new_block_expr (e1);
		append_expr (nb, ne);
		nb->block.result = tmp;
		e1 = nb;
	}
	if (e1->type == ex_error)
		return e1;

	e2 = convert_name (e2);
	if (e2->type == ex_error)
		return e2;

	if (e1->type == ex_bool)
		e1 = convert_from_bool (e1, get_type (e2));
	if (e2->type == ex_bool)
		e2 = convert_from_bool (e2, get_type (e1));

	if ((e = check_precedence (op, e1, e2)))
		return e;

	if (is_reference (get_type (e1))) {
		e1 = pointer_deref (e1);
	}
	if (is_reference (get_type (e2))) {
		e2 = pointer_deref (e2);
	}

	auto t1 = get_type (e1);
	auto t2 = get_type (e2);
	if (!t1 || !t2)
		internal_error (e1, "expr with no type");

	if (is_algebra (t1) || is_algebra (t2)) {
		return algebra_binary_expr (op, e1, e2);
	}

	if (op == QC_EQ || op == QC_NE) {
		if (e1->type == ex_nil) {
			t1 = t2;
			e1 = convert_nil (e1, t1);
		} else if (e2->type == ex_nil) {
			t2 = t1;
			e2 = convert_nil (e2, t2);
		}
	}

	if (is_constant (e1) && is_double (t1) && e1->implicit && is_float (t2)) {
		t1 = float_type (t2);
		e1 = cast_expr (t1, e1);
	}
	if (is_constant (e2) && is_double (t2) && e2->implicit && is_float (t1)) {
		t2 = float_type (t1);
		e2 = cast_expr (t2, e2);
	}
	if (is_array (t1) && (is_pointer (t2) || is_integral (t2))) {
		t1 = pointer_type (dereference_type (t1));
		e1 = cast_expr (t1, e1);
	}
	if (is_array (t2) && (is_pointer (t1) || is_integral (t1))) {
		t1 = pointer_type (dereference_type (t2));
		e2 = cast_expr (t2, e2);
	}

	et1 = low_level_type (t1);
	et2 = low_level_type (t2);

	if (t1->meta >= ty_meta_count || !binary_expr_meta[t1->meta]) {
		return invalid_binary_expr(op, e1, e2);
	}
	if (t2->meta >= ty_meta_count || !binary_expr_meta[t2->meta]) {
		return invalid_binary_expr(op, e1, e2);
	}
	if (binary_expr_meta[t1->meta] != binary_expr_meta[t2->meta]) {
		return invalid_binary_expr(op, e1, e2);
	}
	auto expr_meta = binary_expr_meta[t1->meta];
	if (et1 >= ev_type_count || !expr_meta[et1])
		return invalid_binary_expr(op, e1, e2);
	if (et2 >= ev_type_count || !expr_meta[et1][et2])
		return invalid_binary_expr(op, e1, e2);

	if ((t1->width > 1 || t2->width > 1)) {
		// vector/quaternion and scalar won't get here as vector and quaternion
		// are distict types with type.width == 1, but vector and vec3 WILL get
		// here because of vec3 being float{3}
		if (t1 != t2) {
			auto pt1 = t1;
			auto pt2 = t2;
			if (is_float (base_type (t1)) && is_double (base_type (t2))
				&& e2->implicit) {
				pt2 = promote_type (t1, t2);
			} else if (is_double (base_type (t1)) && is_float (base_type (t2))
					   && e1->implicit) {
				pt1 = promote_type (t2, t1);
			} else if (type_promotes (base_type (t1), base_type (t2))) {
				pt2 = promote_type (t1, t2);
			} else if (type_promotes (base_type (t2), base_type (t1))) {
				pt1 = promote_type (t2, t1);
			} else if (base_type (t1) == base_type (t2)) {
				if (is_vector (t1) || is_quaternion (t1)) {
					pt2 = t1;
				} else if (is_vector (t2) || is_quaternion (t2)) {
					pt1 = t2;
				}
			} else {
				debug (e1, "%d %d\n", e1->implicit, e2->implicit);
				return invalid_binary_expr (op, e1, e2);
			}
			if (pt1 != t1) {
				e1 = cast_expr (pt1, e1);
				t1 = pt1;
			}
			if (pt2 != t2) {
				e2 = cast_expr (pt2, e2);
				t2 = pt2;
			}
		}
		// type_width returns 3/4 for vector/quaternion, but their internal
		// width is only 1
		if (type_width (t1) != t1->width || type_width (t2) != t2->width) {
			et1 = low_level_type (t1);
			et2 = low_level_type (t2);
			goto vector_or_quaternion;
		}
		int         scalar_op = 0;
		if (type_width (t1) == 1) {
			// scalar op vec
			if (!(e = edag_add_expr (convert_scalar (e1, op, e2)))) {
				return invalid_binary_expr (op, e1, e2);
			}
			scalar_op = 1;
			e1 = e;
			t1 = get_type (e1);
		}
		if (type_width (t2) == 1) {
			// vec op scalar
			if (!(e = edag_add_expr (convert_scalar (e2, op, e1)))) {
				return invalid_binary_expr (op, e1, e2);
			}
			scalar_op = 1;
			e2 = e;
			t2 = get_type (e2);
		}
		if (scalar_op && op == '*') {
			op = QC_HADAMARD;
		}
		if (type_width (t1) != type_width (t2)) {
			// vec op vec of different widths
			return invalid_binary_expr (op, e1, e2);
		}
		t1 = get_type (e1);
		t2 = get_type (e2);
		et1 = low_level_type (t1);
		et2 = low_level_type (t2);
		// both widths are the same at this point
		if (t1->width > 1) {
			auto ne = new_binary_expr (op, e1, e2);
			if (is_compare (op)) {
				t1 = int_type (t1);
			}
			if (op == QC_DOT) {
				if (!is_real (t1)) {
					return invalid_binary_expr (op, e1, e2);
				}
				t1 = base_type (t1);
			}
			ne->expr.type = t1;
			return edag_add_expr (ne);
		}
	}
vector_or_quaternion:

	expr_type = expr_meta[et1][et2];
	while (expr_type->op && expr_type->op != op)
		expr_type++;
	if (!expr_type->op)
		return invalid_binary_expr(op, e1, e2);

	if (expr_type->a_cast)
		e1 = cast_expr (expr_type->a_cast, e1);
	if (expr_type->b_cast)
		e2 = cast_expr (expr_type->b_cast, e2);
	if (expr_type->process) {
		e = fold_constants (expr_type->process (op, e1, e2));
		return edag_add_expr (e);
	}

	if ((e = reimplement_binary_expr (op, e1, e2)))
		return edag_add_expr (fold_constants (e));

	if (expr_type->true_op) {
		op = expr_type->true_op;
	}

	auto ne = new_binary_expr (op, e1, e2);
	ne->expr.type = expr_type->result_type;
	if (expr_type->commutative) {
		ne->expr.commutative = expr_type->commutative ();
	}
	if (expr_type->anticommute) {
		ne->expr.anticommute = expr_type->anticommute ();
	}
	if (expr_type->associative) {
		ne->expr.associative = expr_type->associative ();
	}
	if (is_compare (op) || is_logic (op)) {
		if (options.code.progsversion == PROG_ID_VERSION) {
			ne->expr.type = &type_float;
		}
	}
	return edag_add_expr (fold_constants (ne));
}
