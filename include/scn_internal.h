#ifndef __scn_internal_h
#define __scn_internal_h

#include "QF/progs.h"
#include "QF/scene/entity.h"
#include "QF/scene/hierarchy.h"
#include "QF/scene/scene.h"
#include "QF/scene/transform.h"

typedef struct scene_resources_s {
	PR_RESMAP (entity_t) entities;
	PR_RESMAP (hierarchy_t) hierarchies;
	PR_RESMAP (transform_t) transforms;
} scene_resources_t;

#endif//__scn_internal_h