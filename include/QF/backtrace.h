/*
	backtrace.h

	Support for dumping backtrace information.

	Copyright (C) 2023 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2023/12/04

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
#ifndef __QF_backtrace_h
#define __QF_backtrace_h

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_BACKTRACE

#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <backtrace.h>

typedef struct dstring_s dstring_t;

void BT_Init (const char *filename);
void BT_pcInfo (dstring_t *str, uintptr_t pc);
#endif

#endif// __QF_backtrace_h
