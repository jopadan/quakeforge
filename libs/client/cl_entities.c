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

#include "QF/msg.h"

#include "QF/scene/entity.h"
#include "QF/scene/scene.h"
#include "QF/simd/vec4f.h"

#include "QF/plugin/vid_render.h"	//FIXME

#include "QF/scene/entity.h"
#include "QF/scene/scene.h"

#include "client/entities.h"
#include "client/temp_entities.h"
#include "client/world.h"

entitystateset_t cl_static_entities = DARRAY_STATIC_INIT (32);

/*  QW has a max of 512 entities and wants 64 frames of data per entity, plus
	the baseline data (512 * (64 + 1) = 33280), but NQ has a max of 32000
	entities and wants 2 frames of data per entity, plus the baseline data
	(32000 * (2 + 1) = 96000)
*/
#define NUM_ENTITY_STATES 96000

static entity_state_t entity_states[NUM_ENTITY_STATES];
static entity_state_t * const nq_frames[2] = {
	&entity_states[32000 * 1],
	&entity_states[32000 * 2],
};
static entity_state_t * const qw_frames[64] = {
	&entity_states[512 *  1], &entity_states[512 *  2],
	&entity_states[512 *  3], &entity_states[512 *  4],
	&entity_states[512 *  5], &entity_states[512 *  6],
	&entity_states[512 *  7], &entity_states[512 *  8],
	&entity_states[512 *  9], &entity_states[512 * 10],
	&entity_states[512 * 11], &entity_states[512 * 12],
	&entity_states[512 * 13], &entity_states[512 * 14],
	&entity_states[512 * 15], &entity_states[512 * 16],
	&entity_states[512 * 17], &entity_states[512 * 18],
	&entity_states[512 * 19], &entity_states[512 * 20],
	&entity_states[512 * 21], &entity_states[512 * 22],
	&entity_states[512 * 23], &entity_states[512 * 24],
	&entity_states[512 * 25], &entity_states[512 * 26],
	&entity_states[512 * 27], &entity_states[512 * 28],
	&entity_states[512 * 29], &entity_states[512 * 30],
	&entity_states[512 * 31], &entity_states[512 * 32],
	&entity_states[512 * 33], &entity_states[512 * 34],
	&entity_states[512 * 35], &entity_states[512 * 36],
	&entity_states[512 * 37], &entity_states[512 * 38],
	&entity_states[512 * 39], &entity_states[512 * 40],
	&entity_states[512 * 41], &entity_states[512 * 42],
	&entity_states[512 * 43], &entity_states[512 * 44],
	&entity_states[512 * 45], &entity_states[512 * 46],
	&entity_states[512 * 47], &entity_states[512 * 48],
	&entity_states[512 * 49], &entity_states[512 * 50],
	&entity_states[512 * 51], &entity_states[512 * 52],
	&entity_states[512 * 53], &entity_states[512 * 54],
	&entity_states[512 * 55], &entity_states[512 * 56],
	&entity_states[512 * 57], &entity_states[512 * 58],
	&entity_states[512 * 59], &entity_states[512 * 60],
	&entity_states[512 * 61], &entity_states[512 * 62],
	&entity_states[512 * 63], &entity_states[512 * 64],
};
entstates_t nq_entstates = {&entity_states[0], nq_frames, 2, 32000};
entstates_t qw_entstates = {&entity_states[0], qw_frames, 64, 512};

vec3_t ent_colormod[256] = {
	{0, 0, 0},
	{0, 0, 0.333333},
	{0, 0, 0.666667},
	{0, 0, 1},
	{0, 0.142857, 0},
	{0, 0.142857, 0.333333},
	{0, 0.142857, 0.666667},
	{0, 0.142857, 1},
	{0, 0.285714, 0},
	{0, 0.285714, 0.333333},
	{0, 0.285714, 0.666667},
	{0, 0.285714, 1},
	{0, 0.428571, 0},
	{0, 0.428571, 0.333333},
	{0, 0.428571, 0.666667},
	{0, 0.428571, 1},
	{0, 0.571429, 0},
	{0, 0.571429, 0.333333},
	{0, 0.571429, 0.666667},
	{0, 0.571429, 1},
	{0, 0.714286, 0},
	{0, 0.714286, 0.333333},
	{0, 0.714286, 0.666667},
	{0, 0.714286, 1},
	{0, 0.857143, 0},
	{0, 0.857143, 0.333333},
	{0, 0.857143, 0.666667},
	{0, 0.857143, 1},
	{0, 1, 0},
	{0, 1, 0.333333},
	{0, 1, 0.666667},
	{0, 1, 1},
	{0.142857, 0, 0},
	{0.142857, 0, 0.333333},
	{0.142857, 0, 0.666667},
	{0.142857, 0, 1},
	{0.142857, 0.142857, 0},
	{0.142857, 0.142857, 0.333333},
	{0.142857, 0.142857, 0.666667},
	{0.142857, 0.142857, 1},
	{0.142857, 0.285714, 0},
	{0.142857, 0.285714, 0.333333},
	{0.142857, 0.285714, 0.666667},
	{0.142857, 0.285714, 1},
	{0.142857, 0.428571, 0},
	{0.142857, 0.428571, 0.333333},
	{0.142857, 0.428571, 0.666667},
	{0.142857, 0.428571, 1},
	{0.142857, 0.571429, 0},
	{0.142857, 0.571429, 0.333333},
	{0.142857, 0.571429, 0.666667},
	{0.142857, 0.571429, 1},
	{0.142857, 0.714286, 0},
	{0.142857, 0.714286, 0.333333},
	{0.142857, 0.714286, 0.666667},
	{0.142857, 0.714286, 1},
	{0.142857, 0.857143, 0},
	{0.142857, 0.857143, 0.333333},
	{0.142857, 0.857143, 0.666667},
	{0.142857, 0.857143, 1},
	{0.142857, 1, 0},
	{0.142857, 1, 0.333333},
	{0.142857, 1, 0.666667},
	{0.142857, 1, 1},
	{0.285714, 0, 0},
	{0.285714, 0, 0.333333},
	{0.285714, 0, 0.666667},
	{0.285714, 0, 1},
	{0.285714, 0.142857, 0},
	{0.285714, 0.142857, 0.333333},
	{0.285714, 0.142857, 0.666667},
	{0.285714, 0.142857, 1},
	{0.285714, 0.285714, 0},
	{0.285714, 0.285714, 0.333333},
	{0.285714, 0.285714, 0.666667},
	{0.285714, 0.285714, 1},
	{0.285714, 0.428571, 0},
	{0.285714, 0.428571, 0.333333},
	{0.285714, 0.428571, 0.666667},
	{0.285714, 0.428571, 1},
	{0.285714, 0.571429, 0},
	{0.285714, 0.571429, 0.333333},
	{0.285714, 0.571429, 0.666667},
	{0.285714, 0.571429, 1},
	{0.285714, 0.714286, 0},
	{0.285714, 0.714286, 0.333333},
	{0.285714, 0.714286, 0.666667},
	{0.285714, 0.714286, 1},
	{0.285714, 0.857143, 0},
	{0.285714, 0.857143, 0.333333},
	{0.285714, 0.857143, 0.666667},
	{0.285714, 0.857143, 1},
	{0.285714, 1, 0},
	{0.285714, 1, 0.333333},
	{0.285714, 1, 0.666667},
	{0.285714, 1, 1},
	{0.428571, 0, 0},
	{0.428571, 0, 0.333333},
	{0.428571, 0, 0.666667},
	{0.428571, 0, 1},
	{0.428571, 0.142857, 0},
	{0.428571, 0.142857, 0.333333},
	{0.428571, 0.142857, 0.666667},
	{0.428571, 0.142857, 1},
	{0.428571, 0.285714, 0},
	{0.428571, 0.285714, 0.333333},
	{0.428571, 0.285714, 0.666667},
	{0.428571, 0.285714, 1},
	{0.428571, 0.428571, 0},
	{0.428571, 0.428571, 0.333333},
	{0.428571, 0.428571, 0.666667},
	{0.428571, 0.428571, 1},
	{0.428571, 0.571429, 0},
	{0.428571, 0.571429, 0.333333},
	{0.428571, 0.571429, 0.666667},
	{0.428571, 0.571429, 1},
	{0.428571, 0.714286, 0},
	{0.428571, 0.714286, 0.333333},
	{0.428571, 0.714286, 0.666667},
	{0.428571, 0.714286, 1},
	{0.428571, 0.857143, 0},
	{0.428571, 0.857143, 0.333333},
	{0.428571, 0.857143, 0.666667},
	{0.428571, 0.857143, 1},
	{0.428571, 1, 0},
	{0.428571, 1, 0.333333},
	{0.428571, 1, 0.666667},
	{0.428571, 1, 1},
	{0.571429, 0, 0},
	{0.571429, 0, 0.333333},
	{0.571429, 0, 0.666667},
	{0.571429, 0, 1},
	{0.571429, 0.142857, 0},
	{0.571429, 0.142857, 0.333333},
	{0.571429, 0.142857, 0.666667},
	{0.571429, 0.142857, 1},
	{0.571429, 0.285714, 0},
	{0.571429, 0.285714, 0.333333},
	{0.571429, 0.285714, 0.666667},
	{0.571429, 0.285714, 1},
	{0.571429, 0.428571, 0},
	{0.571429, 0.428571, 0.333333},
	{0.571429, 0.428571, 0.666667},
	{0.571429, 0.428571, 1},
	{0.571429, 0.571429, 0},
	{0.571429, 0.571429, 0.333333},
	{0.571429, 0.571429, 0.666667},
	{0.571429, 0.571429, 1},
	{0.571429, 0.714286, 0},
	{0.571429, 0.714286, 0.333333},
	{0.571429, 0.714286, 0.666667},
	{0.571429, 0.714286, 1},
	{0.571429, 0.857143, 0},
	{0.571429, 0.857143, 0.333333},
	{0.571429, 0.857143, 0.666667},
	{0.571429, 0.857143, 1},
	{0.571429, 1, 0},
	{0.571429, 1, 0.333333},
	{0.571429, 1, 0.666667},
	{0.571429, 1, 1},
	{0.714286, 0, 0},
	{0.714286, 0, 0.333333},
	{0.714286, 0, 0.666667},
	{0.714286, 0, 1},
	{0.714286, 0.142857, 0},
	{0.714286, 0.142857, 0.333333},
	{0.714286, 0.142857, 0.666667},
	{0.714286, 0.142857, 1},
	{0.714286, 0.285714, 0},
	{0.714286, 0.285714, 0.333333},
	{0.714286, 0.285714, 0.666667},
	{0.714286, 0.285714, 1},
	{0.714286, 0.428571, 0},
	{0.714286, 0.428571, 0.333333},
	{0.714286, 0.428571, 0.666667},
	{0.714286, 0.428571, 1},
	{0.714286, 0.571429, 0},
	{0.714286, 0.571429, 0.333333},
	{0.714286, 0.571429, 0.666667},
	{0.714286, 0.571429, 1},
	{0.714286, 0.714286, 0},
	{0.714286, 0.714286, 0.333333},
	{0.714286, 0.714286, 0.666667},
	{0.714286, 0.714286, 1},
	{0.714286, 0.857143, 0},
	{0.714286, 0.857143, 0.333333},
	{0.714286, 0.857143, 0.666667},
	{0.714286, 0.857143, 1},
	{0.714286, 1, 0},
	{0.714286, 1, 0.333333},
	{0.714286, 1, 0.666667},
	{0.714286, 1, 1},
	{0.857143, 0, 0},
	{0.857143, 0, 0.333333},
	{0.857143, 0, 0.666667},
	{0.857143, 0, 1},
	{0.857143, 0.142857, 0},
	{0.857143, 0.142857, 0.333333},
	{0.857143, 0.142857, 0.666667},
	{0.857143, 0.142857, 1},
	{0.857143, 0.285714, 0},
	{0.857143, 0.285714, 0.333333},
	{0.857143, 0.285714, 0.666667},
	{0.857143, 0.285714, 1},
	{0.857143, 0.428571, 0},
	{0.857143, 0.428571, 0.333333},
	{0.857143, 0.428571, 0.666667},
	{0.857143, 0.428571, 1},
	{0.857143, 0.571429, 0},
	{0.857143, 0.571429, 0.333333},
	{0.857143, 0.571429, 0.666667},
	{0.857143, 0.571429, 1},
	{0.857143, 0.714286, 0},
	{0.857143, 0.714286, 0.333333},
	{0.857143, 0.714286, 0.666667},
	{0.857143, 0.714286, 1},
	{0.857143, 0.857143, 0},
	{0.857143, 0.857143, 0.333333},
	{0.857143, 0.857143, 0.666667},
	{0.857143, 0.857143, 1},
	{0.857143, 1, 0},
	{0.857143, 1, 0.333333},
	{0.857143, 1, 0.666667},
	{0.857143, 1, 1},
	{1, 0, 0},
	{1, 0, 0.333333},
	{1, 0, 0.666667},
	{1, 0, 1},
	{1, 0.142857, 0},
	{1, 0.142857, 0.333333},
	{1, 0.142857, 0.666667},
	{1, 0.142857, 1},
	{1, 0.285714, 0},
	{1, 0.285714, 0.333333},
	{1, 0.285714, 0.666667},
	{1, 0.285714, 1},
	{1, 0.428571, 0},
	{1, 0.428571, 0.333333},
	{1, 0.428571, 0.666667},
	{1, 0.428571, 1},
	{1, 0.571429, 0},
	{1, 0.571429, 0.333333},
	{1, 0.571429, 0.666667},
	{1, 0.571429, 1},
	{1, 0.714286, 0},
	{1, 0.714286, 0.333333},
	{1, 0.714286, 0.666667},
	{1, 0.714286, 1},
	{1, 0.857143, 0},
	{1, 0.857143, 0.333333},
	{1, 0.857143, 0.666667},
	{1, 0.857143, 1},
	{1, 1, 0},
	{1, 1, 0.333333},
	{1, 1, 0.666667},
	{1, 1, 1}
};

void
CL_TransformEntity (entity_t ent, float scale, const vec3_t angles,
					vec4f_t position)
{
	vec4f_t     rotation;
	vec4f_t     scalevec = { scale, scale, scale, 1};

	if (VectorIsZero (angles)) {
		rotation = (vec4f_t) { 0, 0, 0, 1 };
	} else {
		vec3_t      ang;
		VectorCopy (angles, ang);
		auto renderer = Entity_GetRenderer (ent);
		if (renderer->model && renderer->model->type == mod_mesh) {
			//FIXME use a flag
			// stupid quake bug
			// why, oh, why, do alias models pitch in the opposite direction
			// to everything else?
			ang[PITCH] = -ang[PITCH];
		}
		AngleQuat (ang, (vec_t*)&rotation);//FIXME
	}
	transform_t transform = Entity_Transform (ent);
	Transform_SetLocalTransform (transform, scalevec, rotation, position);
}
