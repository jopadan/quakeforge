/*
	sv_ents.c

	(description)

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

#include "QF/msg.h"
#include "QF/sys.h"

#include "compat.h"
#include "msg_ucmd.h"
#include "net_svc.h"
#include "server.h"
#include "sv_progs.h"


/*
	The PVS must include a small area around the client to allow head
	bobbing or other small motion on the client side.  Otherwise, a bob
	might cause an entity that should be visible to not show up, especially
	when the bob crosses a waterline.
*/

byte        fatpvs[MAX_MAP_LEAFS / 8];
int         fatbytes;


void
SV_AddToFatPVS (vec3_t org, mnode_t *node)
{
	byte       *pvs;
	int         i;
	float       d;
	mplane_t   *plane;

	while (1) {
		// if this is a leaf, accumulate the pvs bits
		if (node->contents < 0) {
			if (node->contents != CONTENTS_SOLID) {
				pvs = Mod_LeafPVS ((mleaf_t *) node, sv.worldmodel);
				for (i = 0; i < fatbytes; i++)
					fatpvs[i] |= pvs[i];
			}
			return;
		}

		plane = node->plane;
		d = DotProduct (org, plane->normal) - plane->dist;
		if (d > 8)
			node = node->children[0];
		else if (d < -8)
			node = node->children[1];
		else {							// go down both
			SV_AddToFatPVS (org, node->children[0]);
			node = node->children[1];
		}
	}
}

/*
	SV_FatPVS

	Calculates a PVS that is the inclusive or of all leafs within 8 pixels
	of the given point.
*/
byte       *
SV_FatPVS (vec3_t org)
{
	fatbytes = (sv.worldmodel->numleafs + 31) >> 3;
	memset (fatpvs, 0, fatbytes);
	SV_AddToFatPVS (org, sv.worldmodel->nodes);
	return fatpvs;
}

// nails are plentiful, so there is a special network protocol for them
#define	MAX_NAILS	32
edict_t    *nails[MAX_NAILS];
int         numnails;


qboolean
SV_AddNailUpdate (edict_t *ent)
{
	if (SVfloat (ent, modelindex) != sv_nailmodel
		&& SVfloat (ent, modelindex) != sv_supernailmodel) return false;
	if (numnails == MAX_NAILS)
		return true;
	nails[numnails] = ent;
	numnails++;
	return true;
}

void
SV_EmitNailUpdate (sizebuf_t *msg)
{
	int         i;
	net_svc_nails_t block;

	if (!numnails)
		return;

	block.numnails = numnails;

	for (i = 0; i < numnails; i++) {
		VectorCopy (SVvector (nails[i], origin), block.nails[i].origin);
		VectorCopy (SVvector (nails[i], angles), block.nails[i].angles);
	}

	MSG_WriteByte (msg, svc_nails);
	NET_SVC_Nails_Emit (&block, msg);
}

unsigned int
SV_EntityState_Diff (entity_state_t *from, entity_state_t *to)
{
	int				i;
	float			miss;
	unsigned int	bits = 0;

	for (i = 0; i < 3; i++) {
		miss = to->origin[i] - from->origin[i];
		if (miss < -0.1 || miss > 0.1)
			bits |= U_ORIGIN1 << i;
	}

	if (to->angles[0] != from->angles[0])
		bits |= U_ANGLE1;

	if (to->angles[1] != from->angles[1])
		bits |= U_ANGLE2;

	if (to->angles[2] != from->angles[2])
		bits |= U_ANGLE3;

	if (to->colormap != from->colormap)
		bits |= U_COLORMAP;

	if (to->skinnum != from->skinnum)
		bits |= U_SKIN;

	if (to->frame != from->frame)
		bits |= U_FRAME;

	if (to->effects != from->effects)
		bits |= U_EFFECTS;

	if (to->modelindex != from->modelindex)
		bits |= U_MODEL;

	// LordHavoc: cleaned up Endy's coding style, and added missing effects
// Ender (QSG - Begin)
//	if (stdver > 1) {
		if (to->alpha != from->alpha)
			bits |= U_ALPHA;

		if (to->scale != from->scale)
			bits |= U_SCALE;

		if (to->glow_size != from->glow_size)
			bits |= U_GLOWSIZE;

		if (to->glow_color != from->glow_color)
			bits |= U_GLOWCOLOR;

		if (to->colormod != from->colormod)
			bits |= U_COLORMOD;
//	}

//	if (bits >= 16777216)
	if (bits & U_GROUP_EXTEND2)
		bits |= U_EXTEND2;

//	if (bits >= 65536)
	if (bits & U_GROUP_EXTEND1)
		bits |= U_EXTEND1;
// Ender (QSG - End)

//	if (bits & 511)
	if (bits & U_GROUP_MOREBITS)
		bits |= U_MOREBITS;

	if (to->flags & U_SOLID)
		bits |= U_SOLID;

	return bits;
}

/*
	SV_WriteDelta

	Writes part of a packetentities message.
	Can delta from either a baseline or a previous packet_entity
*/
void
SV_WriteDelta (entity_state_t *from, entity_state_t *to, sizebuf_t *msg,
			   qboolean force, int stdver)
{
	unsigned int	bits;

	// send an update
	bits = SV_EntityState_Diff (from, to);
	if (stdver <= 1)
		bits &= U_VERSION_ID; // use old the original fields, no extensions

	// write the message
	if (!to->number)
		SV_Error ("Unset entity number");
	if (to->number >= 512)
		SV_Error ("Entity number >= 512");

	if (!bits && !force)
		return;							// nothing to send!
	MSG_WriteShort (msg, to->number | (bits & ~511));

	if (bits & U_MOREBITS)
		MSG_WriteByte (msg, bits & 255);

	// LordHavoc: cleaned up Endy's tabs
// Ender (QSG - Begin)
	if (bits & U_EXTEND1)
		MSG_WriteByte (msg, bits >> 16);
	if (bits & U_EXTEND2)
		MSG_WriteByte (msg, bits >> 24);
// Ender (QSG - End)

	if (bits & U_MODEL)
		MSG_WriteByte (msg, to->modelindex);
	if (bits & U_FRAME)
		MSG_WriteByte (msg, to->frame);
	if (bits & U_COLORMAP)
		MSG_WriteByte (msg, to->colormap);
	if (bits & U_SKIN)
		MSG_WriteByte (msg, to->skinnum);
	if (bits & U_EFFECTS)
		MSG_WriteByte (msg, to->effects);
	if (bits & U_ORIGIN1)
		MSG_WriteCoord (msg, to->origin[0]);
	if (bits & U_ANGLE1)
		MSG_WriteAngle (msg, to->angles[0]);
	if (bits & U_ORIGIN2)
		MSG_WriteCoord (msg, to->origin[1]);
	if (bits & U_ANGLE2)
		MSG_WriteAngle (msg, to->angles[1]);
	if (bits & U_ORIGIN3)
		MSG_WriteCoord (msg, to->origin[2]);
	if (bits & U_ANGLE3)
		MSG_WriteAngle (msg, to->angles[2]);

	// LordHavoc: cleaned up Endy's tabs, rearranged bytes, and implemented
	// missing effects
// Ender (QSG - Begin)
	if (bits & U_ALPHA)
		MSG_WriteByte (msg, to->alpha);
	if (bits & U_SCALE)
		MSG_WriteByte (msg, to->scale);
	if (bits & U_EFFECTS2)
		MSG_WriteByte (msg, (to->effects >> 8));
	if (bits & U_GLOWSIZE)
		MSG_WriteByte (msg, to->glow_size);
	if (bits & U_GLOWCOLOR)
		MSG_WriteByte (msg, to->glow_color);
	if (bits & U_COLORMOD)
		MSG_WriteByte (msg, to->colormod);
	if (bits & U_FRAME2)
		MSG_WriteByte (msg, (to->frame >> 8));
// Ender (QSG - End)
}

/*
	SV_EmitPacketEntities

	Writes an update of a packet_entities_t to the message.
*/
void
SV_EmitPacketEntities (client_t *client, packet_entities_t *to, sizebuf_t *msg)
{
	int         index;
	entity_state_t *baseline;
	net_svc_packetentities_t block;

	block.numwords = block.numdeltas = to->num_entities;

	for (index = 0; index < to->num_entities; index++) {
		baseline = EDICT_NUM (&sv_pr_state,
							  to->entities[index].number)->data;
		block.deltas[index] = to->entities[index];
		block.deltas[index].flags =
			SV_EntityState_Diff (baseline, &to->entities[index]);

		// check if it's a client that doesn't support QSG2
		if (client->stdver <= 1)
			block.deltas[index].flags &= U_VERSION_ID;

		block.words[index] = to->entities[index].number |
							 (block.deltas[index].flags & ~511);
	}

	block.words[index] = 0;
	MSG_WriteByte (msg, svc_packetentities);
	NET_SVC_PacketEntities_Emit (&block, msg);
}

/*
	SV_EmitDeltaPacketEntities

	Writes a delta update of a packet_entities_t to the message.
*/
void
SV_EmitDeltaPacketEntities (client_t *client, packet_entities_t *to,
							sizebuf_t *msg)
{
	int         newindex, oldindex, newnum, oldnum;
	int			word;
	entity_state_t *baseline;
	packet_entities_t *from;
	net_svc_deltapacketentities_t block;

	// this is the frame that we are going to delta update from
	from = &client->frames[client->delta_sequence & UPDATE_MASK].entities;

	block.from = client->delta_sequence;

//	SV_Printf ("---%i to %i ----\n", client->delta_sequence & UPDATE_MASK,
//			   client->netchan.outgoing_sequence & UPDATE_MASK);
	for (newindex = 0, oldindex = 0, word = 0;
		 newindex < to->num_entities || oldindex < from->num_entities;
		 word++) {
		newnum = newindex >= to->num_entities ?
				 9999 : to->entities[newindex].number;
		oldnum = oldindex >= from->num_entities ?
				 9999 : from->entities[oldindex].number;

		if (newnum == oldnum) {		// delta update from old position
//			SV_Printf ("delta %i\n", newnum);
			block.deltas[newindex] = to->entities[newindex];
			block.deltas[newindex].flags =
				SV_EntityState_Diff (&from->entities[oldindex],
									 &to->entities[newindex]);

			// check if it's a client that doesn't support QSG2
			if (client->stdver <= 1)
				block.deltas[newindex].flags &= U_VERSION_ID;

			block.words[word] = newnum | (block.deltas[newindex].flags & ~511);

			oldindex++;
			newindex++;
		} else if (newnum < oldnum) {	// this is a new entity, send
										// it from the baseline
			baseline = EDICT_NUM (&sv_pr_state, newnum)->data;
//			SV_Printf ("baseline %i\n", newnum);
			block.deltas[newindex] = to->entities[newindex];
			block.deltas[newindex].flags =
				SV_EntityState_Diff (baseline, &to->entities[newindex]);

			// check if it's a client that doesn't support QSG2
			if (client->stdver <= 1)
				block.deltas[newindex].flags &= U_VERSION_ID;

			block.words[word] = newnum | (block.deltas[newindex].flags & ~511);

			newindex++;
		} else if (newnum > oldnum) {	// the old entity isn't
										// present in the new message
//			SV_Printf ("remove %i\n", oldnum);
			block.words[word] = oldnum | U_REMOVE;
			oldindex++;
		}
	}

	block.words[word] = 0;
	MSG_WriteByte (msg, svc_deltapacketentities);
	NET_SVC_DeltaPacketEntities_Emit (&block, msg);
}

void
SV_WritePlayersToClient (client_t *client, edict_t *clent, byte * pvs,
						 sizebuf_t *msg)
{
	int         i, j;
	client_t   *cl;
	edict_t    *ent;
	net_svc_playerinfo_t block;

	for (j = 0, cl = svs.clients; j < MAX_CLIENTS; j++, cl++) {
		if (cl->state != cs_spawned)
			continue;

		ent = cl->edict;

		// ZOID visibility tracking
		if (ent != clent &&
			!(client->spec_track && client->spec_track - 1 == j)) {
			if (cl->spectator)
				continue;

			// ignore if not touching a PV leaf
			for (i = 0; i < ent->num_leafs; i++)
				if (pvs[ent->leafnums[i] >> 3] & (1 << (ent->leafnums[i] & 7)))
					break;
			if (i == ent->num_leafs)
				continue;				// not visible
		}

		block.flags = PF_MSEC | PF_COMMAND;

		if (SVfloat (ent, modelindex) != sv_playermodel)
			block.flags |= PF_MODEL;
		for (i = 0; i < 3; i++)
			if (SVvector (ent, velocity)[i])
				block.flags |= PF_VELOCITY1 << i;
		if (SVfloat (ent, effects))
			block.flags |= PF_EFFECTS;
		if (SVfloat (ent, skin))
			block.flags |= PF_SKINNUM;
		if (SVfloat (ent, health) <= 0)
			block.flags |= PF_DEAD;
		if (SVvector (ent, mins)[2] != -24)
			block.flags |= PF_GIB;

		if (cl->spectator) {			// only sent origin and velocity to
										// spectators
			block.flags &= PF_VELOCITY1 | PF_VELOCITY2 | PF_VELOCITY3;
		} else if (ent == clent) {		// don't send a lot of data on
										// personal entity
			block.flags &= ~(PF_MSEC | PF_COMMAND);
			if (SVfloat (ent, weaponframe))
				block.flags |= PF_WEAPONFRAME;
		}

		if (client->spec_track && client->spec_track - 1 == j &&
			SVfloat (ent, weaponframe))
			block.flags |= PF_WEAPONFRAME;

		block.playernum = j;

		VectorCopy (SVvector (ent, origin), block.origin);
		block.frame = SVfloat (ent, frame);

		block.msec = 1000 * (sv.time - cl->localtime);
		if (block.msec > 255)
			block.msec = 255;

		block.usercmd = cl->lastcmd;
		if (SVfloat (ent, health) <= 0) {	// don't show the corpse
											// looking around...
			block.usercmd.angles[0] = 0;
			block.usercmd.angles[1] = SVvector (ent, angles)[1];
			block.usercmd.angles[0] = 0;
		}
		block.usercmd.buttons = 0;			// never send buttons
		block.usercmd.impulse = 0;			// never send impulses

		VectorCopy (SVvector (ent, velocity), block.velocity);
		block.modelindex = SVfloat (ent, modelindex);
		block.skinnum = SVfloat (ent, skin);
		block.effects = SVfloat (ent, effects);
		block.weaponframe = SVfloat (ent, weaponframe);

		MSG_WriteByte (msg, svc_playerinfo);
		NET_SVC_Playerinfo_Emit (&block, msg);
	}
}

/*
	SV_WriteEntitiesToClient

	Encodes the current state of the world as
	a svc_packetentities messages and possibly
	a svc_nails message and
	svc_playerinfo messages
*/
void
SV_WriteEntitiesToClient (client_t *client, sizebuf_t *msg)
{
	byte       *pvs;
	int         e, i;
	vec3_t      org;
	client_frame_t *frame;
	edict_t    *clent, *ent;
	entity_state_t *state;
	packet_entities_t *pack;

	// this is the frame we are creating
	frame = &client->frames[client->netchan.incoming_sequence & UPDATE_MASK];

	// find the client's PVS
	clent = client->edict;
	VectorAdd (SVvector (clent, origin), SVvector (clent, view_ofs), org);
	pvs = SV_FatPVS (org);

	// send over the players in the PVS
	SV_WritePlayersToClient (client, clent, pvs, msg);

	// put other visible entities into either a packet_entities or a nails
	// message
	pack = &frame->entities;
	pack->num_entities = 0;

	numnails = 0;

	for (e = MAX_CLIENTS + 1, ent = EDICT_NUM (&sv_pr_state, e);
		 e < sv.num_edicts;
		 e++, ent = NEXT_EDICT (&sv_pr_state, ent)) {
		if (ent->free)
			continue;

		// ignore ents without visible models
		if (!SVfloat (ent, modelindex)
			|| !*PR_GetString (&sv_pr_state, SVstring (ent, model)))
			continue;

		// ignore if not touching a PV leaf
		for (i = 0; i < ent->num_leafs; i++)
			if (pvs[ent->leafnums[i] >> 3] & (1 << (ent->leafnums[i] & 7)))
				break;

		if (i == ent->num_leafs)
			continue;					// not visible

		if (SV_AddNailUpdate (ent))
			continue;					// added to the special update list

		// add to the packetentities
		if (pack->num_entities == MAX_PACKET_ENTITIES)
			continue;					// all full

		state = &pack->entities[pack->num_entities];
		pack->num_entities++;

		state->number = e;
		state->flags = 0;
		VectorCopy (SVvector (ent, origin), state->origin);
		VectorCopy (SVvector (ent, angles), state->angles);
		state->modelindex = SVfloat (ent, modelindex);
		state->frame = SVfloat (ent, frame);
		state->colormap = SVfloat (ent, colormap);
		state->skinnum = SVfloat (ent, skin);
		state->effects = SVfloat (ent, effects);

		// LordHavoc: cleaned up Endy's coding style, shortened the code,
		// and implemented missing effects
// Ender: EXTEND (QSG - Begin)
		{
			state->alpha = 255;
			state->scale = 16;
			state->glow_size = 0;
			state->glow_color = 254;
			state->colormod = 255;

			if (sv_fields.alpha != -1 && SVfloat (ent, alpha))
				state->alpha = bound (0, SVfloat (ent, alpha), 1) * 255.0;

			if (sv_fields.scale != -1 && SVfloat (ent, scale))
				state->scale = bound (0, SVfloat (ent, scale), 15.9375) * 16.0;

			if (sv_fields.glow_size != -1 && SVfloat (ent, glow_size))
				state->glow_size = bound (-1024, (int) SVfloat
										  (ent, glow_size), 1016) >> 3;

			if (sv_fields.glow_color != -1 && SVvector (ent, glow_color))
				state->glow_color = (int) SVvector (ent, glow_color);

			if (sv_fields.colormod != -1
				&& SVvector (ent, colormod)[0]
				&& SVvector (ent, colormod)[1]
				&& SVvector (ent, colormod)[2])
				state->colormod =
					((int) (bound (0, SVvector (ent, colormod)[0], 1) * 7.0)
					 << 5) |
					((int) (bound (0, SVvector (ent, colormod)[1], 1) * 7.0)
					 << 2) |
					(int) (bound (0, SVvector (ent, colormod)[2], 1) * 3.0);
		}
// Ender: EXTEND (QSG - End)
	}

	// encode the packet entities as a delta from the
	// last packetentities acknowledged by the client
	if (client->delta_sequence != -1)
		SV_EmitDeltaPacketEntities (client, pack, msg);
	else
		SV_EmitPacketEntities (client, pack, msg);

	// now add the specialized nail update
	SV_EmitNailUpdate (msg);
}
