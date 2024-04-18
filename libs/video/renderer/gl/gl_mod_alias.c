/*
	gl_mod_alias.c

	Draw Alias Model

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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "QF/skin.h"

#include "QF/scene/entity.h"

#include "QF/GL/defines.h"
#include "QF/GL/funcs.h"
#include "QF/GL/qf_alias.h"
#include "QF/GL/qf_rlight.h"
#include "QF/GL/qf_rmain.h"
#include "QF/GL/qf_rsurf.h"
#include "QF/GL/qf_vid.h"

#include "compat.h"
#include "mod_internal.h"
#include "r_internal.h"
#include "vid_gl.h"

#define s_dynlight (r_refdef.scene->base + scene_dynlight)

typedef struct {
	vec3_t      normal;
	vec3_t      vert;
} blended_vert_t;

typedef struct {
	blended_vert_t *verts;
	int        *order;
	tex_coord_t *tex_coord;
	int         count;
} vert_order_t;

static vec3_t		shadevector;


static void
GL_DrawAliasFrameTri (vert_order_t *vo)
{
	int         count = vo->count;
	blended_vert_t *verts = vo->verts;
	tex_coord_t *tex_coord = vo->tex_coord;

	qfglBegin (GL_TRIANGLES);
	do {
		// texture coordinates come from the draw list
		qfglTexCoord2fv (tex_coord->st);
		tex_coord++;

		// normals and vertices come from the frame list
		qfglNormal3fv (verts->normal);
		qfglVertex3fv (verts->vert);
		verts++;
	} while (count--);
	qfglEnd ();
}

static inline void
GL_DrawAliasFrameTriMulti (vert_order_t *vo)
{
	int         count = vo->count;
	blended_vert_t *verts = vo->verts;
	tex_coord_t *tex_coord = vo->tex_coord;

	qfglBegin (GL_TRIANGLES);
	do {
		// texture coordinates come from the draw list
		qglMultiTexCoord2fv (gl_mtex_enum + 0, tex_coord->st);
		qglMultiTexCoord2fv (gl_mtex_enum + 1, tex_coord->st);
		tex_coord++;

		// normals and vertices come from the frame list
		qfglNormal3fv (verts->normal);
		qfglVertex3fv (verts->vert);
		verts++;
	} while (--count);
	qfglEnd ();
}

static void
GL_DrawAliasFrame (vert_order_t *vo)
{
	int         count;
	int        *order = vo->order;
	blended_vert_t *verts = vo->verts;

	while ((count = *order++)) {
		// get the vertex count and primitive type
		if (count < 0) {
			count = -count;
			qfglBegin (GL_TRIANGLE_FAN);
		} else {
			qfglBegin (GL_TRIANGLE_STRIP);
		}

		do {
			// texture coordinates come from the draw list
			qfglTexCoord2fv ((float *) order);
			order += 2;

			// normals and vertices come from the frame list
			qfglNormal3fv (verts->normal);
			qfglVertex3fv (verts->vert);
			verts++;
		} while (--count);

		qfglEnd ();
	}
}

static inline void
GL_DrawAliasFrameMulti (vert_order_t *vo)
{
	int         count;
	int        *order = vo->order;
	blended_vert_t *verts = vo->verts;

	while ((count = *order++)) {
		// get the vertex count and primitive type
		if (count < 0) {
			count = -count;
			qfglBegin (GL_TRIANGLE_FAN);
		} else {
			qfglBegin (GL_TRIANGLE_STRIP);
		}

		do {
			// texture coordinates come from the draw list
			qglMultiTexCoord2fv (gl_mtex_enum + 0, (float *) order);
			qglMultiTexCoord2fv (gl_mtex_enum + 1, (float *) order);
			order += 2;

			// normals and vertices come from the frame list
			qfglNormal3fv (verts->normal);
			qfglVertex3fv (verts->vert);
			verts++;
		} while (--count);

		qfglEnd ();
	}
}

/*
	GL_DrawAliasShadowTri

	Standard shadow drawing (triangles version)
*/
static void
GL_DrawAliasShadowTri (transform_t transform, const aliashdr_t *ahdr,
					   const vert_order_t *vo)
{
	int         count = vo->count;
	const blended_vert_t *verts = vo->verts;
	float       height, lheight;
	vec3_t      point;
	const vec_t *scale = ahdr->mdl.scale;
	const vec_t *scale_origin = ahdr->mdl.scale_origin;
	vec4f_t     entorigin;

	entorigin = Transform_GetWorldPosition (transform);
	lheight = entorigin[2] - lightspot[2];
	height = -lheight + 1.0;

	qfglBegin (GL_TRIANGLES);

	do {
		// normals and vertices come from the frame list
		point[0] = verts->vert[0] * scale[0] + scale_origin[0];
		point[1] = verts->vert[1] * scale[1] + scale_origin[1];
		point[2] = verts->vert[2] * scale[2] + scale_origin[2] + lheight;

		point[0] -= shadevector[0] * point[2];
		point[1] -= shadevector[1] * point[2];
		point[2] = height;
		qfglVertex3fv (point);

		verts++;
	} while (--count);

	qfglEnd ();
}

/*
	GL_DrawAliasShadow

	Standard shadow drawing
*/
static void
GL_DrawAliasShadow (transform_t transform, const aliashdr_t *ahdr,
				    const vert_order_t *vo)
{
	float       height, lheight;
	int         count;
	const int  *order = vo->order;
	vec3_t      point;
	const blended_vert_t *verts = vo->verts;
	vec4f_t     entorigin;

	entorigin = Transform_GetWorldPosition (transform);
	lheight = entorigin[2] - lightspot[2];
	height = -lheight + 1.0;

	while ((count = *order++)) {
		// get the vertex count and primitive type
		if (count < 0) {
			count = -count;
			qfglBegin (GL_TRIANGLE_FAN);
		} else
			qfglBegin (GL_TRIANGLE_STRIP);

		order += 2 * count;		// skip texture coords
		do {
			// normals and vertices come from the frame list
			point[0] =
				verts->vert[0] * ahdr->mdl.scale[0] +
				ahdr->mdl.scale_origin[0];
			point[1] =
				verts->vert[1] * ahdr->mdl.scale[1] +
				ahdr->mdl.scale_origin[1];
			point[2] =
				verts->vert[2] * ahdr->mdl.scale[2] +
				ahdr->mdl.scale_origin[2] + lheight;

			point[0] -= shadevector[0] * point[2];
			point[1] -= shadevector[1] * point[2];
			point[2] = height;
			qfglVertex3fv (point);

			verts++;
		} while (--count);

		qfglEnd ();
	}
}

static inline vert_order_t *
GL_GetAliasFrameVerts16 (aliashdr_t *ahdr, entity_t e)
{
	auto animation = Entity_GetAnimation (e);
	float         blend = R_AliasGetLerpedFrames (animation, ahdr);
	int           count, i;
	trivertx16_t *verts;
	vert_order_t *vo;
	blended_vert_t *vo_v;


	verts = (trivertx16_t *) ((byte *) ahdr + ahdr->posedata);

	count = ahdr->poseverts;
	vo = Hunk_TempAlloc (0, sizeof (*vo) + count * sizeof (blended_vert_t));
	vo->order = (int *) ((byte *) ahdr + ahdr->commands);
	vo->verts = (blended_vert_t *) &vo[1];
	if (ahdr->tex_coord) {
		vo->tex_coord = (tex_coord_t *) ((byte *) ahdr + ahdr->tex_coord);
	} else {
		vo->tex_coord = NULL;
	}
	vo->count = count;

	if (!gl_lerp_anim)
		blend = 1.0;


	if (blend == 0.0) {
		verts = verts + animation->pose1 * count;
	} else if (blend == 1.0) {
		verts = verts + animation->pose2 * count;
	} else {
		trivertx16_t *verts1, *verts2;

		verts1 = verts + animation->pose1 * count;
		verts2 = verts + animation->pose2 * count;

		for (i = 0, vo_v = vo->verts; i < count;
			 i++, vo_v++, verts1++, verts2++) {
			float      *n1, *n2;

			VectorBlend (verts1->v, verts2->v, blend, vo_v->vert);
			n1 = r_avertexnormals[verts1->lightnormalindex];
			n2 = r_avertexnormals[verts2->lightnormalindex];
			VectorBlend (n1, n2, blend, vo_v->normal);
			if (VectorIsZero (vo_v->normal)) {
				if (blend < 0.5) {
					VectorCopy (n1, vo_v->normal);
				} else {
					VectorCopy (n2, vo_v->normal);
				}
			}
		}
		return vo;
	}

	for (i = 0, vo_v = vo->verts; i < count; i++, vo_v++, verts++) {
		VectorCopy (verts->v, vo_v->vert);
		VectorCopy (r_avertexnormals[verts->lightnormalindex], vo_v->normal);
	}
	return vo;
}

static inline vert_order_t *
GL_GetAliasFrameVerts (aliashdr_t *ahdr, entity_t e)
{
	auto animation = Entity_GetAnimation (e);
	float       blend = R_AliasGetLerpedFrames (animation, ahdr);
	int         count, i;
	trivertx_t *verts;
	vert_order_t *vo;
	blended_vert_t *vo_v;


	verts = (trivertx_t *) ((byte *) ahdr + ahdr->posedata);

	count = ahdr->poseverts;
	vo = Hunk_TempAlloc (0, sizeof (*vo) + count * sizeof (blended_vert_t));
	vo->order = (int *) ((byte *) ahdr + ahdr->commands);
	vo->verts = (blended_vert_t *) &vo[1];
	if (ahdr->tex_coord) {
		vo->tex_coord = (tex_coord_t *) ((byte *) ahdr + ahdr->tex_coord);
	} else {
		vo->tex_coord = NULL;
	}
	vo->count = count;

	if (!gl_lerp_anim)
		blend = 1.0;

	if (blend == 0.0) {
		verts = verts + animation->pose1 * count;
	} else if (blend == 1.0) {
		verts = verts + animation->pose2 * count;
	} else {
		trivertx_t *verts1, *verts2;

		verts1 = verts + animation->pose1 * count;
		verts2 = verts + animation->pose2 * count;

		for (i = 0, vo_v = vo->verts; i < count;
			 i++, vo_v++, verts1++, verts2++) {
			float      *n1, *n2;

			VectorBlend (verts1->v, verts2->v, blend, vo_v->vert);
			n1 = r_avertexnormals[verts1->lightnormalindex];
			n2 = r_avertexnormals[verts2->lightnormalindex];
			VectorBlend (n1, n2, blend, vo_v->normal);
			if (VectorIsZero (vo_v->normal)) {
				if (blend < 0.5) {
					VectorCopy (n1, vo_v->normal);
				} else {
					VectorCopy (n2, vo_v->normal);
				}
			}
		}
		return vo;
	}

	for (i = 0, vo_v = vo->verts; i < count; i++, vo_v++, verts++) {
		VectorCopy (verts->v, vo_v->vert);
		VectorCopy (r_avertexnormals[verts->lightnormalindex], vo_v->normal);
	}
	return vo;
}

static glskin_t
gl_get_skin (entity_t e, renderer_t *renderer, aliashdr_t *ahdr)
{
	auto colormap = Entity_GetColormap (e);
	if (!gl_nocolors) {
		skin_t *skin = renderer->skin ? Skin_Get (renderer->skin) : nullptr;
		if (skin) {
			return gl_Skin_Get (skin->tex, colormap, (byte *) ahdr);
		}
	}
	maliasskindesc_t *skindesc;
	auto animation = Entity_GetAnimation (e);
	skindesc = R_AliasGetSkindesc (animation, renderer->skinnum, ahdr);
	if (!skindesc->texnum) {
		auto tex = (tex_t *) ((byte *) ahdr + skindesc->skin);
		return gl_Skin_Get (tex, colormap, (byte *) ahdr);
	}
	return (glskin_t) { .id = skindesc->texnum, .fb = skindesc->fb_texnum };
}

void
gl_R_DrawAliasModel (entity_t e)
{
	float       radius, minlight, d;
	float       position[4] = {0.0, 0.0, 0.0, 1.0},
				color[4] = {0.0, 0.0, 0.0, 1.0},
				dark[4] = {0.0, 0.0, 0.0, 1.0},
				emission[4] = {0.0, 0.0, 0.0, 1.0};
	int         gl_light;
	int         used_lights = 0;
	bool        is_fullbright = false;
	aliashdr_t *ahdr;
	vec3_t      dist, scale;
	vec4f_t     origin;
	vert_order_t *vo;
	auto renderer = Entity_GetRenderer (e);
	model_t    *model = renderer->model;

	if (renderer->onlyshadows) {
		return;
	}

	radius = model->radius;
	transform_t transform = Entity_Transform (e);
	origin = Transform_GetWorldPosition (transform);
	VectorCopy (Transform_GetWorldScale (transform), scale);
	//FIXME assumes uniform scale
	if (scale[0] != 1.0) {
		radius *= scale[0];
	}
	if (R_CullSphere (r_refdef.frustum, (vec_t*)&origin, radius)) {//FIXME
		return;
	}

	gl_modelalpha = renderer->colormod[3];

	is_fullbright = (model->fullbright || renderer->fullbright);
	minlight = max (model->min_light, renderer->min_light);

	qfglColor4fv (renderer->colormod);

	if (!is_fullbright) {
		float lightadj;

		// get lighting information
		R_LightPoint (&r_refdef.worldmodel->brush, origin);//FIXME

		lightadj = (ambientcolor[0] + ambientcolor[1] + ambientcolor[2]) / 765.0;

		// Do minlight stuff here since that's how software does it :)

		if (lightadj > 0) {
			if (lightadj < minlight)
				lightadj = minlight / lightadj;
			else
				lightadj = 1.0;

			// 255 is fullbright
			VectorScale (ambientcolor, lightadj / 255.0, ambientcolor);
		} else {
			ambientcolor[0] = ambientcolor[1] = ambientcolor[2] = minlight;
		}

		if (gl_vector_light) {
			auto dlight_pool = &r_refdef.registry->comp_pools[s_dynlight];
			auto dlight_data = (dlight_t *) dlight_pool->data;
			for (uint32_t i = 0; i < dlight_pool->count; i++) {
				auto l = &dlight_data[i];
				VectorSubtract (l->origin, origin, dist);
				if ((d = DotProduct (dist, dist)) >	// Out of range
					((l->radius + radius) * (l->radius + radius))) {
					continue;
				}


				if (used_lights >= gl_max_lights) {
					// For solid lighting, multiply by 0.5 since it's cos
					// 60 and 60 is a good guesstimate at the average
					// incident angle. Seems to match vector lighting
					// best, too.
					VectorMultAdd (emission,
						0.5 / ((d * 0.01 / l->radius) + 0.5),
						l->color, emission);
					continue;
				}

				VectorCopy (l->origin, position);

				VectorCopy (l->color, color);
				color[3] = 1.0;

				gl_light = GL_LIGHT0 + used_lights;
				qfglEnable (gl_light);
				qfglLightfv (gl_light, GL_POSITION, position);
				qfglLightfv (gl_light, GL_AMBIENT, color);
				qfglLightfv (gl_light, GL_DIFFUSE, color);
				qfglLightfv (gl_light, GL_SPECULAR, color);
				// 0.01 is used here because it just seemed to match
				// the bmodel lighting best. it's over r instead of r*r
				// so that larger-radiused lights will be brighter
				qfglLightf (gl_light, GL_QUADRATIC_ATTENUATION,
							0.01 / (l->radius));
				used_lights++;
			}

			VectorAdd (ambientcolor, emission, emission);
			d = max (emission[0], max (emission[1], emission[2]));
			// 1.5 to allow some pastelization (curb darkness from dlight)
			if (d > 1.5) {
				VectorScale (emission, 1.5 / d, emission);
			}

			qfglMaterialfv (GL_FRONT, GL_EMISSION, emission);
		} else {
			VectorCopy (ambientcolor, emission);

			auto dlight_pool = &r_refdef.registry->comp_pools[s_dynlight];
			auto dlight_data = (dlight_t *) dlight_pool->data;
			for (uint32_t i = 0; i < dlight_pool->count; i++) {
				auto l = &dlight_data[i];
				VectorSubtract (l->origin, origin, dist);

				if ((d = DotProduct (dist, dist)) > (l->radius + radius) *
					(l->radius + radius)) {
						continue;
				}

				// For solid lighting, multiply by 0.5 since it's cos 60
				// and 60 is a good guesstimate at the average incident
				// angle. Seems to match vector lighting best, too.
				VectorMultAdd (emission,
					(0.5 / ((d * 0.01 / l->radius) + 0.5)),
					l->color, emission);
			}

			d = max (emission[0], max (emission[1], emission[2]));
			// 1.5 to allow some fading (curb emission making stuff dark)
			if (d > 1.5) {
				VectorScale (emission, 1.5 / d, emission);
			}

			emission[0] *= renderer->colormod[0];
			emission[1] *= renderer->colormod[1];
			emission[2] *= renderer->colormod[2];
			emission[3] *= renderer->colormod[3];

			qfglColor4fv (emission);
		}
	}

	// locate the proper data
	if (!(ahdr = renderer->model->aliashdr)) {
		ahdr = Cache_Get (&renderer->model->cache);
	}
	gl_ctx->alias_polys += ahdr->mdl.numtris;

	// if the model has a colorised/external skin, use it, otherwise use
	// the skin embedded in the model data
	auto glskin = gl_get_skin (e, renderer, ahdr);
	if (!gl_fb_models || is_fullbright) {
		glskin.fb = 0;
	}

	if (ahdr->mdl.ident == HEADER_MDL16) {
		// because we multipled by 256 when we loaded the verts, we have to
		// scale by 1/256 when drawing.
		//FIXME see scaling above
		VectorScale (ahdr->mdl.scale, 1 / 256.0, scale);
		vo = GL_GetAliasFrameVerts16 (ahdr, e);
	} else {
		//FIXME see scaling above
		VectorScale (ahdr->mdl.scale, 1, scale);
		vo = GL_GetAliasFrameVerts (ahdr, e);
	}

	// setup the transform
	qfglPushMatrix ();
	gl_R_RotateForEntity (Transform_GetWorldMatrixPtr (transform));

	qfglTranslatef (ahdr->mdl.scale_origin[0],
					ahdr->mdl.scale_origin[1],
					ahdr->mdl.scale_origin[2]);
	qfglScalef (scale[0], scale[1], scale[2]);

	if (gl_modelalpha < 1.0)
		qfglDepthMask (GL_FALSE);

	// draw all the triangles
	if (is_fullbright) {
		qfglBindTexture (GL_TEXTURE_2D, glskin.id);

		if (gl_vector_light) {
			qfglDisable (GL_LIGHTING);
			if (!gl_tess)
				qfglDisable (GL_NORMALIZE);
		}

		if (vo->tex_coord)
			GL_DrawAliasFrameTri (vo);
		else
			GL_DrawAliasFrame (vo);

		if (gl_vector_light) {
			if (!gl_tess)
				qfglEnable (GL_NORMALIZE);
			qfglEnable (GL_LIGHTING);
		}
	} else if (!glskin.fb) {
		// Model has no fullbrights, don't bother with multi
		qfglBindTexture (GL_TEXTURE_2D, glskin.id);
		if (vo->tex_coord)
			GL_DrawAliasFrameTri (vo);
		else
			GL_DrawAliasFrame (vo);
	} else {	// try multitexture
		if (gl_mtex_active_tmus >= 2) {	// set up the textures
			qglActiveTexture (gl_mtex_enum + 0);
			qfglBindTexture (GL_TEXTURE_2D, glskin.id);

			qglActiveTexture (gl_mtex_enum + 1);
			qfglEnable (GL_TEXTURE_2D);
			qfglBindTexture (GL_TEXTURE_2D, glskin.fb);

			// do the heavy lifting
			if (vo->tex_coord)
				GL_DrawAliasFrameTriMulti (vo);
			else
				GL_DrawAliasFrameMulti (vo);

			// restore the settings
			qfglDisable (GL_TEXTURE_2D);
			qglActiveTexture (gl_mtex_enum + 0);
		} else {
			if (vo->tex_coord) {
				qfglBindTexture (GL_TEXTURE_2D, glskin.id);
				GL_DrawAliasFrameTri (vo);

				if (gl_vector_light) {
					qfglDisable (GL_LIGHTING);
					if (!gl_tess)
						qfglDisable (GL_NORMALIZE);
				}

				qfglColor4fv (renderer->colormod);

				qfglBindTexture (GL_TEXTURE_2D, glskin.fb);
				GL_DrawAliasFrameTri (vo);

				if (gl_vector_light) {
					qfglEnable (GL_LIGHTING);
					if (!gl_tess)
						qfglEnable (GL_NORMALIZE);
				}
			} else {
				qfglBindTexture (GL_TEXTURE_2D, glskin.id);
				GL_DrawAliasFrame (vo);

				if (gl_vector_light) {
					qfglDisable (GL_LIGHTING);
					if (!gl_tess)
						qfglDisable (GL_NORMALIZE);
				}

				qfglColor4fv (renderer->colormod);

				qfglBindTexture (GL_TEXTURE_2D, glskin.fb);
				GL_DrawAliasFrame (vo);

				if (gl_vector_light) {
					qfglEnable (GL_LIGHTING);
					if (!gl_tess)
						qfglEnable (GL_NORMALIZE);
				}
			}
		}
	}

	qfglPopMatrix ();

	// torches, grenades, and lightning bolts do not have shadows
	if (r_shadows && model->shadow_alpha) {
		mat4f_t     shadow_mat;

		qfglPushMatrix ();
		gl_R_RotateForEntity (Transform_GetWorldMatrixPtr (transform));

		if (!gl_tess)
			qfglDisable (GL_NORMALIZE);
		qfglDisable (GL_LIGHTING);
		qfglDisable (GL_TEXTURE_2D);
		qfglDepthMask (GL_FALSE);

		if (gl_modelalpha < 1.0) {
			VectorBlend (renderer->colormod, dark, 0.5, color);
			color[3] = gl_modelalpha * (model->shadow_alpha / 255.0);
			qfglColor4fv (color);
		} else {
			color_black[3] = model->shadow_alpha;
			qfglColor4ubv (color_black);
		}
		//FIXME fully vectorize
		vec4f_t     vec = { 0.707106781, 0, 0.707106781, 0 };
		Transform_GetWorldMatrix (transform, shadow_mat);
		mat4ftranspose (shadow_mat, shadow_mat);
		vec = m3vmulf (shadow_mat, vec);
		VectorCopy (vec, shadevector);
		if (vo->tex_coord)
			GL_DrawAliasShadowTri (transform, ahdr, vo);
		else
			GL_DrawAliasShadow (transform, ahdr, vo);

		qfglDepthMask (GL_TRUE);
		qfglEnable (GL_TEXTURE_2D);
		qfglEnable (GL_LIGHTING);
		if (!gl_tess)
			qfglEnable (GL_NORMALIZE);
		qfglPopMatrix ();
	} else if (gl_modelalpha < 1.0) {
		qfglDepthMask (GL_TRUE);
	}

	while (used_lights--) {
		qfglDisable (GL_LIGHT0 + used_lights);
	}

	if (!renderer->model->aliashdr) {
		Cache_Release (&renderer->model->cache);
	}
}
