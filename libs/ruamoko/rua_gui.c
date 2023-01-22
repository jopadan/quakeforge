/*
	r_gui.c

	Ruamoko GUI support functions

	Copyright (C) 2022       Bill Currie <bill@taniwha.org>

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
#include <stdlib.h>

#include "QF/draw.h"
#include "QF/progs.h"
#include "QF/quakefs.h"
#include "QF/render.h"

#include "QF/ui/font.h"
#include "QF/ui/passage.h"
#include "QF/ui/text.h"
#include "QF/ui/view.h"

#include "QF/plugin/vid_render.h"

#include "rua_internal.h"

typedef struct rua_passage_s {
	struct rua_passage_s *next;
	struct rua_passage_s **prev;
	passage_t  *passage;
} rua_passage_t;

typedef struct rua_font_s {
	struct rua_font_s *next;
	struct rua_font_s **prev;
	font_t     *font;
} rua_font_t;

typedef struct {
	progs_t    *pr;
	PR_RESMAP (rua_passage_t) passage_map;
	rua_passage_t *passages;
	PR_RESMAP (rua_font_t) font_map;
	rua_font_t *fonts;

	ecs_registry_t *reg;
	uint32_t    text_base;
	uint32_t    view_base;
	uint32_t    passage_base;
} gui_resources_t;

static rua_passage_t *
passage_new (gui_resources_t *res)
{
	return PR_RESNEW (res->passage_map);
}

static void
passage_free (gui_resources_t *res, rua_passage_t *passage)
{
	PR_RESFREE (res->passage_map, passage);
}

static void
passage_reset (gui_resources_t *res)
{
	PR_RESRESET (res->passage_map);
}

static inline rua_passage_t *
passage_get (gui_resources_t *res, int index)
{
	return PR_RESGET(res->passage_map, index);
}

static inline int __attribute__((pure))
passage_index (gui_resources_t *res, rua_passage_t *passage)
{
	return PR_RESINDEX(res->passage_map, passage);
}

static int
alloc_passage (gui_resources_t *res, passage_t *passage)
{
	rua_passage_t *psg = passage_new (res);

	psg->next = res->passages;
	psg->prev = &res->passages;
	if (res->passages) {
		res->passages->prev = &psg->next;
	}
	res->passages = psg;
	psg->passage = passage;
	return passage_index (res, psg);
}

static rua_passage_t * __attribute__((pure))
_get_passage (gui_resources_t *res, int handle, const char *func)
{
	rua_passage_t *psg = passage_get (res, handle);
	if (!psg) {
		PR_RunError (res->pr, "invalid passage handle passed to %s", func);
	}
	return psg;
}
#define get_passage(res, handle) _get_passage (res, handle, __FUNCTION__ + 3)

static rua_font_t *
font_new (gui_resources_t *res)
{
	return PR_RESNEW (res->font_map);
}
/*
static void
font_free (gui_resources_t *res, rua_font_t *font)
{
	PR_RESFREE (res->font_map, font);
}
*/
static void
font_reset (gui_resources_t *res)
{
	PR_RESRESET (res->font_map);
}

static inline rua_font_t *
font_get (gui_resources_t *res, int index)
{
	return PR_RESGET(res->font_map, index);
}

static inline int __attribute__((pure))
font_index (gui_resources_t *res, rua_font_t *font)
{
	return PR_RESINDEX(res->font_map, font);
}

static int
alloc_font (gui_resources_t *res, font_t *font)
{
	rua_font_t *psg = font_new (res);

	psg->next = res->fonts;
	psg->prev = &res->fonts;
	if (res->fonts) {
		res->fonts->prev = &psg->next;
	}
	res->fonts = psg;
	psg->font = font;
	return font_index (res, psg);
}

static rua_font_t * __attribute__((pure))
_get_font (gui_resources_t *res, int handle, const char *func)
{
	rua_font_t *psg = font_get (res, handle);
	if (!psg) {
		PR_RunError (res->pr, "invalid font handle passed to %s", func);
	}
	return psg;
}
#define get_font(res, handle) _get_font (res, handle, __FUNCTION__ + 3)

static void
bi_gui_clear (progs_t *pr, void *_res)
{
	gui_resources_t *res = _res;

	rua_passage_t *psg;
	for (psg = res->passages; psg; psg = psg->next) {
		Passage_Delete (psg->passage);
	}
	res->passages = 0;
	passage_reset (res);

	rua_font_t *font;
	for (font = res->fonts; font; font = font->next) {
		//FIXME can't unload fonts
	}
	res->fonts = 0;
	font_reset (res);
}

static void
bi_gui_destroy (progs_t *pr, void *_res)
{
	gui_resources_t *res = _res;
	ECS_DelRegistry (res->reg);
	free (res);
}

#define bi(x) static void bi_##x (progs_t *pr, void *_res)

bi (Font_Load)
{
	gui_resources_t *res = _res;
	const char *font_path = P_GSTRING (pr, 0);
	int         font_size = P_INT (pr, 1);

	QFile      *font_file = QFS_FOpenFile (font_path);
	font_t     *font = Font_Load (font_file, font_size);
	R_INT (pr) = alloc_font (res, font);
}

bi (Passage_New)
{
	gui_resources_t *res = _res;
	ecs_system_t passage_sys = { .reg = res->reg, .base = res->passage_base };
	passage_t  *passage = Passage_New (passage_sys);
	R_INT (pr) = alloc_passage (res, passage);
}

bi (Passage_ParseText)
{
	gui_resources_t *res = _res;
	int         handle = P_INT (pr, 0);
	rua_passage_t *psg = get_passage (res, handle);
	const char *text = P_GSTRING (pr, 1);
	Passage_ParseText (psg->passage, text);
}

bi (Passage_Delete)
{
	gui_resources_t *res = _res;
	int         handle = P_INT (pr, 0);
	rua_passage_t *psg = get_passage (res, handle);
	Passage_Delete (psg->passage);
	passage_free (res, psg);
}

bi (Passage_ChildCount)
{
	gui_resources_t *res = _res;
	rua_passage_t *psg = get_passage (res, P_INT (pr, 0));

	uint32_t    par = P_UINT (pr, 1);
	R_UINT (pr) = psg->passage->hierarchy->childCount[par];
}

bi (Passage_GetChild)
{
	gui_resources_t *res = _res;
	rua_passage_t *psg = get_passage (res, P_INT (pr, 0));

	hierarchy_t *h = psg->passage->hierarchy;
	uint32_t    par = P_UINT (pr, 1);
	uint32_t    index = P_UINT (pr, 2);
	R_UINT (pr) = h->ent[h->childIndex[par] + index];
}

bi (Text_View)
{
	gui_resources_t *res = _res;
	rua_font_t *font = get_font (res, P_INT (pr, 0));
	rua_passage_t *psg = get_passage (res, P_INT (pr, 1));
	ecs_system_t viewsys = { .reg = res->reg, .base = res->view_base };
	view_t      view = Text_View (viewsys, font->font, psg->passage);
	R_INT (pr) = view.id;//FIXME
}

bi (Text_SetScript)
{
	gui_resources_t *res = _res;
	uint32_t    textent = P_UINT (pr, 0);
	const char *lang = P_GSTRING (pr, 1);
	int         script = P_INT (pr, 1);
	int         dir = P_INT (pr, 1);
	ecs_system_t textsys = { .reg = res->reg, .base = res->text_base };
	Text_SetScript (textsys, textent, lang, script, dir);
}

static void
draw_glyphs (view_pos_t *abs, glyphset_t *glyphset, glyphref_t *gref)
{
	uint32_t    count = gref->count;
	glyphobj_t *glyph = glyphset->glyphs + gref->start;

	while (count-- > 0) {
		glyphobj_t *g = glyph++;
		r_funcs->Draw_Glyph (abs->x + g->x, abs->y + g->y,
							 g->fontid, g->glyphid, 254);
	}
}

static void
draw_box (view_pos_t *abs, view_pos_t *len, uint32_t ind, int c)
{
	int x = abs[ind].x;
	int y = abs[ind].y;
	int w = len[ind].x;
	int h = len[ind].y;
	r_funcs->Draw_Line (x, y, x + w, y, c);
	r_funcs->Draw_Line (x, y + h, x + w, y + h, c);
	r_funcs->Draw_Line (x, y, x, y + h, c);
	r_funcs->Draw_Line (x + w, y, x + w, y + h, c);
}

bi (Text_Draw)
{
	gui_resources_t *res = _res;
	uint32_t    passage_glyphs = res->text_base + text_passage_glyphs;
	uint32_t    glyphs = res->text_base + text_glyphs;
	uint32_t    vhref = res->view_base + view_href;
	ecs_pool_t *pool = &res->reg->comp_pools[passage_glyphs];
	uint32_t    count = pool->count;
	uint32_t   *ent = pool->dense;
	glyphset_t *glyphset = pool->data;

	while (count-- > 0) {
		view_t      psg_view = { .id = *ent++, .reg = res->reg, .comp = vhref};
		// first child is always a paragraph view, and all views after the
		// first paragraph's first child are all text views
		view_t      para_view = View_GetChild (psg_view, 0);
		view_t      text_view = View_GetChild (para_view, 0);
		hierref_t  *href = View_GetRef (text_view);
		glyphset_t *gs = glyphset++;
		hierarchy_t *h = href->hierarchy;
		view_pos_t *abs = h->components[view_abs];
		view_pos_t *len = h->components[view_len];

		for (uint32_t i = href->index; i < h->num_objects; i++) {
			glyphref_t *gref = Ent_GetComponent (h->ent[i], glyphs, res->reg);
			draw_glyphs (&abs[i], gs, gref);

			if (0) draw_box (abs, len, i, 253);
		}
		if (0) {
			for (uint32_t i = 1; i < href->index; i++) {
				draw_box (abs, len, i, 251);
			}
		}
	}
}

bi (View_ChildCount)
{
	gui_resources_t *res = _res;
	uint32_t    viewid = P_UINT (pr, 0);
	view_t      view = { .id = viewid, .reg = res->reg,
						 .comp = res->view_base + view_href };
	R_UINT (pr) = View_ChildCount (view);
}

bi (View_GetChild)
{
	gui_resources_t *res = _res;
	uint32_t    viewid = P_UINT (pr, 0);
	uint32_t    index = P_UINT (pr, 1);
	view_t      view = { .id = viewid, .reg = res->reg,
						 .comp = res->view_base + view_href };
	R_UINT (pr) = View_GetChild (view, index).id;
}

bi (View_SetPos)
{
	gui_resources_t *res = _res;
	uint32_t    viewid = P_UINT (pr, 0);
	int         x = P_INT (pr, 1);
	int         y = P_INT (pr, 2);
	view_t      view = { .id = viewid, .reg = res->reg,
						 .comp = res->view_base + view_href };
	View_SetPos (view, x, y);
}

bi (View_SetLen)
{
	gui_resources_t *res = _res;
	uint32_t    viewid = P_UINT (pr, 0);
	int         x = P_INT (pr, 1);
	int         y = P_INT (pr, 2);
	view_t      view = { .id = viewid, .reg = res->reg,
						 .comp = res->view_base + view_href };
	View_SetLen (view, x, y);
}

bi (View_GetLen)
{
	gui_resources_t *res = _res;
	uint32_t    viewid = P_UINT (pr, 0);
	view_t      view = { .id = viewid, .reg = res->reg,
						 .comp = res->view_base + view_href };
	view_pos_t  l = View_GetLen (view);
	R_var (pr, ivec2) = (pr_ivec2_t) { l.x, l.y };
}

bi (View_UpdateHierarchy)
{
	gui_resources_t *res = _res;
	uint32_t    viewid = P_UINT (pr, 0);
	view_t      view = { .id = viewid, .reg = res->reg,
						 .comp = res->view_base + view_href };
	View_UpdateHierarchy (view);
}

#undef bi
#define bi(x,np,params...) {#x, bi_##x, -1, np, {params}}
#define p(type) PR_PARAM(type)
#define P(a, s) { .size = (s), .alignment = BITOP_LOG2 (a), }
static builtin_t builtins[] = {
	bi(Font_Load,           2, p(string), p(int)),
	bi(Passage_New,         0),
	bi(Passage_ParseText,   2, p(ptr), p(string)),
	bi(Passage_Delete,      1, p(ptr)),
	bi(Passage_ChildCount,  2, p(ptr), p(uint)),
	bi(Passage_GetChild,    3, p(ptr), p(uint), p(uint)),

	bi(Text_View,           2, p(ptr), p(int)),
	bi(Text_SetScript,      4, p(uint), p(string), p(int), p (int)),

	bi(View_ChildCount,     1, p(uint)),
	bi(View_GetChild,       2, p(uint), p(uint)),
	bi(View_SetPos,         3, p(uint), p(int), p(int)),
	bi(View_SetLen,         3, p(uint), p(int), p(int)),
	bi(View_GetLen,         1, p(uint)),
	bi(View_UpdateHierarchy,1, p(uint)),

	bi(Text_Draw,           0),

	{0}
};

void
RUA_GUI_Init (progs_t *pr, int secure)
{
	gui_resources_t *res = calloc (1, sizeof (gui_resources_t));
	res->pr = pr;

	PR_Resources_Register (pr, "Draw", res, bi_gui_clear, bi_gui_destroy);
	PR_RegisterBuiltins (pr, builtins, res);

	res->reg = ECS_NewRegistry ();
	res->text_base = ECS_RegisterComponents (res->reg, text_components,
											 text_comp_count);
	res->view_base = ECS_RegisterComponents (res->reg, view_components,
											 view_comp_count);
	res->passage_base = ECS_RegisterComponents (res->reg, passage_components,
												passage_comp_count);
	ECS_CreateComponentPools (res->reg);
}