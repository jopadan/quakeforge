/*
	#FILENAME#

	#DESCRIPTION#

	Copyright (C) 2001 #AUTHOR#

	Author: #AUTHOR#
	Date: #DATE#

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

	$Id$
*/

#ifndef __method_h
#define __method_h

#include "function.h"

typedef struct method_s {
	struct method_s *next;
	int         instance;
	param_t    *selector;
	param_t    *params;
	struct type_s *type;
	struct def_s *def;
	struct function_s *func;
	char       *name;
	char       *types;
} method_t;

typedef struct methodlist_s {
	method_t   *head;
	method_t  **tail;
} methodlist_t;

typedef struct keywordarg_s {
	// the first two fields match the first two fiels of param_t in
	// functionl.h
	struct keywordarg_s *next;
	const char *selector;
	expr_t     *expr;
} keywordarg_t;

struct class_s;
struct expr_s;
struct dstring_s;

method_t *new_method (struct type_s *ret_type, param_t *selector,
					  param_t *opt_parms);
void add_method (methodlist_t *methodlist, method_t *method);
struct def_s *method_def (struct class_s *class, method_t *method);

methodlist_t *new_methodlist (void);
void copy_methods (methodlist_t *dst, methodlist_t *src);
int method_compare (method_t *m1, method_t *m2);

keywordarg_t *new_keywordarg (const char *selector, struct expr_s *expr);

struct expr_s *send_message (int super);

void selector_name (struct dstring_s *sel_id, keywordarg_t *selector);
void selector_types (struct dstring_s *sel_types, keywordarg_t *selector);
struct def_s *selector_def (const char *sel_id, const char *sel_types);

struct def_s *emit_methods (methodlist_t *methods, const char *name,
							int instance);

void clear_selectors (void);

#endif//__method_h
