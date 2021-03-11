/*
	cl_effect.c

	Client side effect management

	Copyright (C) 1996-1997  Id Software, Inc.
	Copyright (C) 2021 Bill Currie <bill@taniwha.org>

	Author: Bill Currie <bill@taniwha.org>
	Date: 2021/3/11

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

#include "QF/entity.h"
#include "QF/render.h"

#include "QF/plugin/vid_render.h"	//FIXME

#include "client/entities.h"
#include "client/effects.h"

void
CL_NewDlight (int key, vec3_t org, int effects, byte glow_size,
			  byte glow_color, double time)
{
	float       radius;
	dlight_t   *dl;
	static quat_t normal = {0.4, 0.2, 0.05, 0.7};
	static quat_t red = {0.5, 0.05, 0.05, 0.7};
	static quat_t blue = {0.05, 0.05, 0.5, 0.7};
	static quat_t purple = {0.5, 0.05, 0.5, 0.7};

	effects &= EF_BLUE | EF_RED | EF_BRIGHTLIGHT | EF_DIMLIGHT;
	if (!effects) {
		if (!glow_size)
			return;
	}

	dl = r_funcs->R_AllocDlight (key);
	if (!dl)
		return;
	VectorCopy (org, dl->origin);

	if (effects & (EF_BLUE | EF_RED | EF_BRIGHTLIGHT | EF_DIMLIGHT)) {
		radius = 200 + (rand () & 31);
		if (effects & EF_BRIGHTLIGHT) {
			radius += 200;
			dl->origin[2] += 16;
		}
		if (effects & EF_DIMLIGHT)
			if (effects & ~EF_DIMLIGHT)
				radius -= 100;
		dl->radius = radius;
		dl->die = time + 0.1;

		switch (effects & (EF_RED | EF_BLUE)) {
			case EF_RED | EF_BLUE:
				QuatCopy (purple, dl->color);
				break;
			case EF_RED:
				QuatCopy (red, dl->color);
				break;
			case EF_BLUE:
				QuatCopy (blue, dl->color);
				break;
			default:
				QuatCopy (normal, dl->color);
				break;
		}
	}

	if (glow_size) {
		dl->radius += glow_size < 128 ? glow_size * 8.0 :
			(glow_size - 256) * 8.0;
		dl->die = time + 0.1;
		if (glow_color) {
			if (glow_color == 255) {
				dl->color[0] = dl->color[1] = dl->color[2] = 1.0;
			} else {
				byte        *tempcolor;

				tempcolor = (byte *) &d_8to24table[glow_color];
				VectorScale (tempcolor, 1 / 255.0, dl->color);
			}
		}
	}
}

void
CL_ModelEffects (entity_t *ent, int num, int glow_color, double time)
{
	dlight_t   *dl;
	model_t    *model = ent->renderer.model;

	// add automatic particle trails
	if (model->flags & EF_ROCKET) {
		dl = r_funcs->R_AllocDlight (num);
		if (dl) {
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200.0;
			dl->die = time + 0.1;
			//FIXME VectorCopy (r_firecolor->vec, dl->color);
			VectorSet (0.9, 0.7, 0.0, dl->color);
			dl->color[3] = 0.7;
		}
		r_funcs->particles->R_RocketTrail (ent);
	} else if (model->flags & EF_GRENADE)
		r_funcs->particles->R_GrenadeTrail (ent);
	else if (model->flags & EF_GIB)
		r_funcs->particles->R_BloodTrail (ent);
	else if (model->flags & EF_ZOMGIB)
		r_funcs->particles->R_SlightBloodTrail (ent);
	else if (model->flags & EF_TRACER)
		r_funcs->particles->R_WizTrail (ent);
	else if (model->flags & EF_TRACER2)
		r_funcs->particles->R_FlameTrail (ent);
	else if (model->flags & EF_TRACER3)
		r_funcs->particles->R_VoorTrail (ent);
	else if (model->flags & EF_GLOWTRAIL)
		if (r_funcs->particles->R_GlowTrail)
			r_funcs->particles->R_GlowTrail (ent, glow_color);
}

void
CL_MuzzleFlash (vec4f_t position, vec4f_t fv, float zoffset, int num,
				double time)
{
	dlight_t   *dl = r_funcs->R_AllocDlight (num);
	if (dl) {
		position += 18 * fv;
		VectorCopy (position, dl->origin);
		dl->origin[2] += zoffset;
		dl->radius = 200 + (rand () & 31);
		dl->die = time + 0.1;
		dl->minlight = 32;
		dl->color[0] = 0.2;
		dl->color[1] = 0.1;
		dl->color[2] = 0.05;
		dl->color[3] = 0.7;
	}
}

void
CL_EntityEffects (int num, entity_t *ent, entity_state_t *state, double time)
{
	if (state->effects & EF_BRIGHTFIELD)
		r_funcs->particles->R_EntityParticles (ent);
	if (state->effects & EF_MUZZLEFLASH) {
		vec4f_t     position = Transform_GetWorldPosition (ent->transform);
		vec4f_t     fv = Transform_Forward (ent->transform);
		CL_MuzzleFlash (position, fv, 16, num, time);
	}
}
