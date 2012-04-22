/*
	trace.c

	(description)

	Copyright (C) 1996-1997  Id Software, Inc.
	Copyright (C) 2002 Colin Thompson

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

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_IO_H
# include <io.h>
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <getopt.h>
#include <errno.h>
#include <stdlib.h>

#include "QF/bspfile.h"
#include "QF/dstring.h"
#include "QF/mathlib.h"
#include "QF/qtypes.h"
#include "QF/quakefs.h"
#include "QF/sys.h"
#include "QF/va.h"

#include "light.h"
#include "threads.h"
#include "entities.h"
#include "options.h"
#include "properties.h"

options_t	options;
bsp_t *bsp;

dstring_t *bspfile;
dstring_t *litfile;

float scalecos = 0.5;

dstring_t *lightdata;
dstring_t *rgblightdata;

dmodel_t *bspmodel;
int bspfileface;		// next surface to dispatch
int bspfileent;			// next entity to dispatch

vec3_t bsp_origin;

qboolean extrasamples;

float minlights[MAX_MAP_FACES];


int
GetFileSpace (int size)
{
	int ofs;

	LOCK;
	lightdata->size = (lightdata->size + 3) & ~3;
	ofs = lightdata->size;
	lightdata->size += size;
	dstring_adjust (lightdata);

	rgblightdata->size = (ofs + size) * 3;
	dstring_adjust (rgblightdata);
	UNLOCK;
	return ofs;
}

static void *
VisThread (void *junk)
{
	int         i;

	while (1) {
		LOCK;
		i = bspfileent++;
		UNLOCK;
		if (i >= num_entities)
			return 0;
		VisEntity (i);
	}
}

static void *
LightThread (void *l)
{
	int         i;

	while (1) {
		LOCK;
		i = bspfileface++;
		if (i < bsp->numfaces) {
			printf ("%5d / %d\r", i, bsp->numfaces);
			fflush (stdout);
		}
		UNLOCK;
		if (i >= bsp->numfaces)
			return 0;

		LightFace (l, i);
	}
}

static void
LightWorld (void)
{
	int         i, j;
	vec3_t      org;
	entity_t   *ent;
	const char *name;

	lightdata = dstring_new ();
	rgblightdata = dstring_new ();
	surfacelightchain = (lightchain_t **) calloc (bsp->numfaces,
												  sizeof (lightchain_t *));
	surfaceorgs = (vec3_t *) calloc (bsp->numfaces, sizeof (vec3_t));

	for (i = 1; i < bsp->nummodels; i++) {
		ent = FindEntityWithKeyPair ("model", name = va ("*%d", i));
		VectorZero (org);
		if (!ent)
			Sys_Error ("FindFaceOffsets: Couldn't find entity for model %s.\n",
					   name);
		if (!strncmp (ValueForKey (ent, "classname"), "rotate_", 7))
			GetVectorForKey (ent, "origin", org);
		for (j = 0; j < bsp->models[i].numfaces; j++)
			VectorCopy (org, surfaceorgs[i]);
	}

	VisThread (0);	// not worth threading :/
	VisStats ();
	RunThreadsOn (LightThread);

	BSP_AddLighting (bsp, (byte *) lightdata->str, lightdata->size);

	if (options.verbosity >= 0)
		printf ("lightdatasize: %ld\n", (long) bsp->lightdatasize);
}

int
main (int argc, char **argv)
{
	double      start, stop;
	QFile      *f;

	start = Sys_DoubleTime ();

	this_program = argv[0];

	DecodeArgs (argc, argv);	

	if (!bspfile) {
		fprintf (stderr, "%s: no bsp file specified.\n", this_program);
		usage (1);
	}

    InitThreads ();

	QFS_SetExtension (bspfile, ".bsp");

	litfile = dstring_strdup (bspfile->str);
	QFS_SetExtension (litfile, ".lit");

	if (options.properties_filename)
		LoadProperties (options.properties_filename);

	f = Qopen (bspfile->str, "rbz");
	if (!f)
		Sys_Error ("could not open %s for reading", bspfile->str);
	bsp = LoadBSPFile (f, Qfilesize (f));
	Qclose (f);
	LoadEntities ();

	MakeTnodes (&bsp->models[0]);

	LightWorld ();

	WriteEntitiesToString ();

	f = Qopen (bspfile->str, "wb");
	if (!f)
		Sys_Error ("could not open %s for writing", bspfile->str);
	WriteBSPFile (bsp, f);
	Qclose (f);

	dstring_insert (rgblightdata, 0, "QLIT\x01\x00\x00\x00", 8);

	f = Qopen (litfile->str, "wb");
	if (!f)
		Sys_Error ("could not open %s for writing", litfile->str);
	Qwrite (f, rgblightdata->str, rgblightdata->size);
	Qclose (f);

	stop = Sys_DoubleTime ();
	
	if (options.verbosity >= 0)
		printf ("%5.1f seconds elapsed\n", stop - start);

	return 0;
}
