/*
	r_screen.h

	shared screen declarations

	Copyright (C) 2002 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2002/12/11

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

#ifndef __r_screen_h
#define __r_screen_h

#include "QF/vid.h"

extern int         scr_copytop;
extern int         scr_copyeverything;

extern float       scr_con_current;
extern float       scr_conlines;				// lines of console to display

extern int         oldscreensize;
extern float       oldfov;
extern int         oldsbar;

extern qboolean    scr_initialized;			// ready to draw

extern struct qpic_s *scr_ram;
extern struct qpic_s *scr_net;
extern struct qpic_s *scr_turtle;

extern int         scr_fullupdate;

extern int         clearconsole;
extern int         clearnotify;

extern viddef_t    vid;						// global video state

extern vrect_t    *pconupdate;
extern vrect_t     scr_vrect;

extern qboolean    scr_skipupdate;

/* CENTER PRINTING */

extern char        scr_centerstring[1024];
extern float       scr_centertime_start;		// for slow victory printing
extern float       scr_centertime_off;
extern int         scr_center_lines;
extern int         scr_erase_lines;
extern int         scr_erase_center;

float CalcFov (float fov_x, float width, float height);
void SCR_SetUpToDrawConsole (void);
void SCR_ScreenShot_f (void);

#endif//__r_screen_h
