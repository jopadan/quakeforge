/*
	bi_string.c

	CSQC string builtins

	Copyright (C) 1996-1997  Id Software, Inc.

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

#include "QF/progs.h"
#include "QF/zone.h"

/*
    bi_String_ReplaceChar
    
    Repalces a special character in a string with another
*/
static void
bi_String_ReplaceChar (progs_t *pr)
{
	char        old = G_INT (pr, OFS_PARM0);
	char        new = G_INT (pr, OFS_PARM1);
	const char *src = G_STRING (pr, OFS_PARM2);
	const char *s;
	char       *dst = Hunk_TempAlloc (strlen (src) + 1);
	char       *d;

	for (d = dst, s = src; *s; d++, s++) {
		if (*s == old)
			*d = new;
		else
			*d = *s;
	}
	RETURN_STRING (pr, dst);
}

/*
    bi_String_Cut
    
    Cuts a specified part from a string
*/
static void
bi_String_Cut (progs_t *pr)
{
	char        pos = G_INT (pr, OFS_PARM0);
	char        len = G_INT (pr, OFS_PARM1);
	const char *str = G_STRING (pr, OFS_PARM2);
	char       *dst = Hunk_TempAlloc ((strlen (str) - len) + 1);
	int			cnt;

	memset (dst, 0, (strlen (str) - len) + 1);
	strncpy(dst, str, pos);
	str += pos;
	for (cnt = 0; cnt < len; cnt++) 
		str++;
	strcpy(dst, str);
	RETURN_STRING (pr, dst);
}

/*
    bi_String_Len
    
    Gives back the length of the string
*/
static void
bi_String_Len (progs_t *pr)
{
	const char *str = G_STRING (pr, OFS_PARM0);

    G_INT (pr, OFS_RETURN) = strlen(str);
}

void
String_Progs_Init (progs_t *pr)
{
	PR_AddBuiltin (pr, "String_ReplaceChar", bi_String_ReplaceChar, -1);
	PR_AddBuiltin (pr, "String_Cut", bi_String_Cut, -1);
	PR_AddBuiltin (pr, "String_Len", bi_String_Len, -1);
}
