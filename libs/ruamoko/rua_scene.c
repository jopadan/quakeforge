/*
	bi_scene.c

	Ruamoko scene builtins

	Copyright (C) 2022 Bill Currie <bill@taniwha.org>

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

#include <stdlib.h>
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "QF/cmem.h"
#include "QF/hash.h"
#include "QF/progs.h"

#include "QF/scene/entity.h"
#include "QF/scene/scene.h"
#include "QF/scene/transform.h"

#include "rua_internal.h"

typedef struct rua_scene_s {
	struct rua_scene_s *next;
	struct rua_scene_s **prev;
	scene_t    *scene;
} rua_scene_t;

typedef struct rua_scene_resources_s {
	progs_t    *pr;
	PR_RESMAP (rua_scene_t) scene_map;
	rua_scene_t *scenes;
} rua_scene_resources_t;

static rua_scene_t *
rua_scene_new (rua_scene_resources_t *res)
{
	return PR_RESNEW (res->scene_map);
}

static void
rua_scene_free (rua_scene_resources_t *res, rua_scene_t *scene)
{
	if (scene->next) {
		scene->next->prev = scene->prev;
	}
	*scene->prev = scene->next;
	scene->prev = 0;
	PR_RESFREE (res->scene_map, scene);
}

static rua_scene_t * __attribute__((pure))
rua__scene_get (rua_scene_resources_t *res, int id, const char *name)
{
	rua_scene_t *scene = PR_RESGET (res->scene_map, id);

	// scene->prev will be null if the handle is unallocated
	if (!scene || !scene->prev) {
		PR_RunError (res->pr, "invalid scene passed to %s", name + 3);
	}
	return scene;
}
#define rua_scene_get(res, id) rua__scene_get(res, id, __FUNCTION__)

static entity_t * __attribute__((pure))
rua__entity_get (progs_t *pr, rua_scene_t *scene, int id, const char *name)
{
	entity_t   *ent = Scene_GetEntity (scene->scene, id);

	if (!ent) {
		PR_RunError (pr, "invalid entity passed to %s", name + 3);
	}
	return ent;
}
#define rua_entity_get(pr, scene, id) rua__entity_get(pr, scene, id, __FUNCTION__)

static transform_t * __attribute__((pure))
rua__transform_get (progs_t *pr, rua_scene_t *scene, int id, const char *name)
{
	transform_t *transform = Scene_GetTransform (scene->scene, id);

	if (!transform) {
		PR_RunError (pr, "invalid transform passed to %s", name + 3);
	}
	return transform;
}
#define rua_transform_get(pr, scene, id) \
	rua__transform_get(pr, scene, id, __FUNCTION__)

static int __attribute__((pure))
rua_scene_index (rua_scene_resources_t *res, rua_scene_t *scene)
{
	return PR_RESINDEX (res->scene_map, scene);
}

static void
bi_Scene_NewScene (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;

	rua_scene_t *scene = rua_scene_new (res);

	scene->scene = Scene_NewScene ();

	scene->next = res->scenes;
	if (res->scenes) {
		res->scenes->prev = &scene->next;
	}
	scene->prev = &res->scenes;
	res->scenes = scene;

	R_INT (pr) = rua_scene_index (res, scene);
}

static void
rua_delete_scene (rua_scene_resources_t *res, rua_scene_t *scene)
{
	Scene_DeleteScene (scene->scene);
	rua_scene_free (res, scene);
}

static void
bi_Scene_DeleteScene (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));

	rua_delete_scene (res, scene);
}

static void
bi_Scene_CreateEntity (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	entity_t   *ent = Scene_CreateEntity (scene->scene);
	R_INT (pr) = ent->id;
}

static void
bi_Scene_DestroyEntity (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	entity_t   *ent = rua_entity_get (pr, scene, P_INT (pr, 1));
	Scene_DestroyEntity (scene->scene, ent);
}

static void
bi_Entity_GetTransform (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	entity_t   *ent = rua_entity_get (pr, scene, P_INT (pr, 1));

	R_INT (pr) = ent->transform->id;
}

static void
bi_Transform_ChildCount (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));

	R_UINT (pr) = Transform_ChildCount (transform);
}

static void
bi_Transform_GetChild (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	transform_t *child = Transform_GetChild (transform, P_UINT (pr, 2));

	R_UINT (pr) = child ? child->id : 0;
}

static void
bi_Transform_SetParent (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	transform_t *parent = rua_transform_get (pr, scene, P_INT (pr, 2));

	Transform_SetParent (transform, parent);
}

static void
bi_Transform_GetParent (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	transform_t *parent = Transform_GetParent (transform);

	R_INT (pr) = parent ? parent->id : 0;
}

static void
bi_Transform_SetTag (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	pr_uint_t   tag = P_UINT (pr, 2);
	Transform_SetTag (transform, tag);
}

static void
bi_Transform_GetTag (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));

	R_UINT (pr) = Transform_GetTag (transform);
}

static void
bi_Transform_GetLocalMatrix (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	Transform_GetLocalMatrix (transform, &R_PACKED (pr, pr_vec4_t));
}

static void
bi_Transform_GetLocalInverse (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	Transform_GetLocalInverse (transform, &R_PACKED (pr, pr_vec4_t));
}

static void
bi_Transform_GetWorldMatrix (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	Transform_GetWorldMatrix (transform, &R_PACKED (pr, pr_vec4_t));
}

static void
bi_Transform_GetWorldInverse (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	Transform_GetWorldInverse (transform, &R_PACKED (pr, pr_vec4_t));
}

static void
bi_Transform_SetLocalPosition (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	Transform_SetLocalPosition (transform, P_PACKED (pr, pr_vec4_t, 2));
}

static void
bi_Transform_GetLocalPosition (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	R_PACKED (pr, pr_vec4_t) = Transform_GetLocalPosition (transform);
}

static void
bi_Transform_SetLocalRotation (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	Transform_SetLocalRotation (transform, P_PACKED (pr, pr_vec4_t, 2));
}

static void
bi_Transform_GetLocalRotation (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	R_PACKED (pr, pr_vec4_t) = Transform_GetLocalRotation (transform);
}

static void
bi_Transform_SetLocalScale (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	Transform_SetLocalScale (transform, P_PACKED (pr, pr_vec4_t, 2));
}

static void
bi_Transform_GetLocalScale (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	R_PACKED (pr, pr_vec4_t) = Transform_GetLocalScale (transform);
}

static void
bi_Transform_SetWorldPosition (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	Transform_SetWorldPosition (transform, P_PACKED (pr, pr_vec4_t, 2));
}

static void
bi_Transform_GetWorldPosition (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	R_PACKED (pr, pr_vec4_t) = Transform_GetWorldPosition (transform);
}

static void
bi_Transform_SetWorldRotation (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	Transform_SetWorldRotation (transform, P_PACKED (pr, pr_vec4_t, 2));
}

static void
bi_Transform_GetWorldRotation (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	R_PACKED (pr, pr_vec4_t) = Transform_GetWorldRotation (transform);
}

static void
bi_Transform_GetWorldScale (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	R_PACKED (pr, pr_vec4_t) = Transform_GetWorldScale (transform);
}

static void
bi_Transform_SetLocalTransform (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	Transform_SetLocalTransform (transform, P_PACKED (pr, pr_vec4_t, 2),
			P_PACKED (pr, pr_vec4_t, 3), P_PACKED (pr, pr_vec4_t, 4));
}

static void
bi_Transform_Forward (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	R_PACKED (pr, pr_vec4_t) = Transform_Forward (transform);
}

static void
bi_Transform_Right (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	R_PACKED (pr, pr_vec4_t) = Transform_Right (transform);
}

static void
bi_Transform_Up (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;
	rua_scene_t *scene = rua_scene_get (res, P_INT (pr, 0));
	transform_t *transform = rua_transform_get (pr, scene, P_INT (pr, 1));
	R_PACKED (pr, pr_vec4_t) = Transform_Up (transform);
}

#define p(type) PR_PARAM(type)
#define P(a, s) { .size = (s), .alignment = BITOP_LOG2 (a), }
#define bi(x,np,params...) {#x, bi_##x, -1, np, {params}}
static builtin_t builtins[] = {
	bi(Scene_NewScene,      0),
	bi(Scene_DeleteScene,   1, p(ptr)),
	bi(Scene_CreateEntity,  1, p(ptr)),
	bi(Scene_DestroyEntity, 2, p(ptr), p(ptr)),

	bi(Entity_GetTransform, 2, p(ptr), p(ptr)),

	bi(Transform_ChildCount,        2, p(ptr), p(ptr)),
	bi(Transform_GetChild,          3, p(ptr), p(ptr), p(int)),
	bi(Transform_SetParent,         3, p(ptr), p(ptr), p(ptr)),
	bi(Transform_GetParent,         2, p(ptr), p(ptr)),

	bi(Transform_SetTag,            3, p(ptr), p(ptr), p(uint)),
	bi(Transform_GetTag,            2, p(ptr), p(ptr)),

	bi(Transform_GetLocalMatrix,    2, p(ptr), p(ptr)),
	bi(Transform_GetLocalInverse,   2, p(ptr), p(ptr)),
	bi(Transform_GetWorldMatrix,    2, p(ptr), p(ptr)),
	bi(Transform_GetWorldInverse,   2, p(ptr), p(ptr)),

	bi(Transform_SetLocalPosition,  3, p(ptr), p(ptr), p(vec4)),
	bi(Transform_GetLocalPosition,  2, p(ptr), p(ptr)),
	bi(Transform_SetLocalRotation,  3, p(ptr), p(ptr), p(vec4)),
	bi(Transform_GetLocalRotation,  2, p(ptr), p(ptr)),
	bi(Transform_SetLocalScale,     3, p(ptr), p(ptr), p(vec4)),
	bi(Transform_GetLocalScale,     2, p(ptr), p(ptr)),

	bi(Transform_SetWorldPosition,  3, p(ptr), p(ptr), p(vec4)),
	bi(Transform_GetWorldPosition,  2, p(ptr), p(ptr)),
	bi(Transform_SetWorldRotation,  3, p(ptr), p(ptr), p(vec4)),
	bi(Transform_GetWorldRotation,  2, p(ptr), p(ptr)),
	bi(Transform_GetWorldScale,     2, p(ptr), p(ptr)),

	bi(Transform_SetLocalTransform, 5, p(ptr), p(ptr),
	                                   p(vec4), p(vec4), p(vec4)),
	bi(Transform_Forward,           2, p(ptr), p(ptr)),
	bi(Transform_Right,             2, p(ptr), p(ptr)),
	bi(Transform_Up,                2, p(ptr), p(ptr)),

	{0}
};

static void
bi_scene_clear (progs_t *pr, void *_res)
{
	rua_scene_resources_t *res = _res;

	while (res->scenes) {
		rua_delete_scene (res, res->scenes);
	}
}

void
RUA_Scene_Init (progs_t *pr, int secure)
{
	rua_scene_resources_t *res = calloc (sizeof (rua_scene_resources_t), 1);

	res->pr = pr;

	PR_Resources_Register (pr, "scene", res, bi_scene_clear);
	PR_RegisterBuiltins (pr, builtins, res);
}