/*
	cl_entities.c

	Client side entity management

	Copyright (C) 2012 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2012/6/28

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

#include "QF/cbuf.h"
#include "QF/cmd.h"
#include "QF/idparse.h"
#include "QF/quakefs.h"
#include "QF/plist.h"
#include "QF/progs.h"
#include "QF/msg.h"

#include "QF/scene/entity.h"
#include "QF/scene/scene.h"
#include "QF/simd/vec4f.h"

#include "QF/plugin/vid_render.h"	//FIXME

#include "client/entities.h"
#include "client/temp_entities.h"
#include "client/world.h"

worldscene_t cl_world = {
	.models = DARRAY_STATIC_INIT (32),
};

void
CL_World_Init (void)
{
	cl_world.scene = Scene_NewScene ();
}

void
CL_ParseBaseline (qmsg_t *msg, entity_state_t *baseline, int version)
{
	int         bits = 0;

	if (version == 2)
		bits = MSG_ReadByte (msg);

	if (bits & B_LARGEMODEL)
		baseline->modelindex = MSG_ReadShort (msg);
	else
		baseline->modelindex = MSG_ReadByte (msg);

	if (bits & B_LARGEFRAME)
		baseline->frame = MSG_ReadShort (msg);
	else
		baseline->frame = MSG_ReadByte (msg);

	baseline->colormap = MSG_ReadByte (msg);
	baseline->skinnum = MSG_ReadByte (msg);

	MSG_ReadCoordAngleV (msg, (vec_t*)&baseline->origin, baseline->angles);//FIXME
	baseline->origin[3] = 1;//FIXME

	if (bits & B_ALPHA)
		baseline->alpha = MSG_ReadByte (msg);
	else
		baseline->alpha = 255;//FIXME alpha
	baseline->scale = 16;
	baseline->glow_size = 0;
	baseline->glow_color = 254;
	baseline->colormod = 255;
}

void
CL_ParseStatic (qmsg_t *msg, int version)
{
	entity_t	   *ent;
	entity_state_t	es;

	ent = Scene_CreateEntity (cl_world.scene);
	CL_Init_Entity (ent);

	CL_ParseBaseline (msg, &es, version);
	DARRAY_APPEND (&cl_static_entities, es);

	// copy it to the current state
	ent->renderer.model = cl_world.models.a[es.modelindex];
	ent->animation.frame = es.frame;
	ent->renderer.skinnum = es.skinnum;

	CL_TransformEntity (ent, es.scale / 16.0, es.angles, es.origin);

	R_AddEfrags (&cl_world.worldmodel->brush, ent);
}

static void
map_cfg (const char *mapname, int all)
{
	char       *name = malloc (strlen (mapname) + 4 + 1);
	cbuf_t     *cbuf = Cbuf_New (&id_interp);
	QFile      *f;

	QFS_StripExtension (mapname, name);
	strcat (name, ".cfg");
	f = QFS_FOpenFile (name);
	if (f) {
		Qclose (f);
		Cmd_Exec_File (cbuf, name, 1);
	} else {
		Cmd_Exec_File (cbuf, "maps_default.cfg", 1);
	}
	if (all) {
		Cbuf_Execute_Stack (cbuf);
	} else {
		Cbuf_Execute_Sets (cbuf);
	}
	free (name);
	Cbuf_Delete (cbuf);
}

void
CL_MapCfg (const char *mapname)
{
	map_cfg (mapname, 0);
}

static plitem_t *
map_ent (const char *mapname)
{
	static progs_t edpr;
	char       *name = malloc (strlen (mapname) + 4 + 1);
	char       *buf;
	plitem_t   *edicts = 0;
	QFile      *ent_file;

	QFS_StripExtension (mapname, name);
	strcat (name, ".ent");
	ent_file = QFS_VOpenFile (name, 0, cl_world.models.a[1]->vpath);
	if ((buf = (char *) QFS_LoadFile (ent_file, 0))) {
		edicts = ED_Parse (&edpr, buf);
		free (buf);
	} else {
		edicts = ED_Parse (&edpr, cl_world.models.a[1]->brush.entities);
	}
	free (name);
	return edicts;
}

static void
CL_LoadSky (const char *name)
{
	plitem_t   *worldspawn = cl_world.worldspawn;
	plitem_t   *item;
	static const char *sky_keys[] = {
		"sky",      // Q2/DarkPlaces
		"skyname",  // old QF
		"qlsky",    // QuakeLives
		0
	};

	if (!name) {
		if (!worldspawn) {
			r_funcs->R_LoadSkys (0);
			return;
		}
		for (const char **key = sky_keys; *key; key++) {
			if ((item = PL_ObjectForKey (cl_world.worldspawn, *key))) {
				name = PL_String (item);
				break;
			}
		}
	}
	r_funcs->R_LoadSkys (name);
}

void
CL_World_NewMap (const char *mapname, const char *skyname)
{
	cl_static_entities.size = 0;
	r_funcs->R_NewMap (cl_world.worldmodel,
					   cl_world.models.a, cl_world.models.size);
	if (cl_world.models.a[1] && cl_world.models.a[1]->brush.entities) {
		if (cl_world.edicts) {
			PL_Free (cl_world.edicts);
		}
		cl_world.edicts = map_ent (mapname);
		if (cl_world.edicts) {
			cl_world.worldspawn = PL_ObjectAtIndex (cl_world.edicts, 0);
			CL_LoadSky (skyname);
			Fog_ParseWorldspawn (cl_world.worldspawn);
		}
	}
	map_cfg (mapname, 1);
}