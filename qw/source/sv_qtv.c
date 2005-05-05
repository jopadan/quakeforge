/*
	#FILENAME#

	#DESCRIPTION#

	Copyright (C) 2004 #AUTHOR#

	Author: #AUTHOR#
	Date: #DATE#

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

static __attribute__ ((unused)) const char rcsid[] =
	"$Id$";

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "QF/cmd.h"
#include "QF/cbuf.h"
#include "QF/dstring.h"
#include "QF/hash.h"
#include "QF/idparse.h"
#include "QF/info.h"
#include "QF/va.h"

#include "compat.h"
#include "server.h"
#include "sv_qtv.h"
#include "sv_recorder.h"

typedef struct {
	netchan_t   netchan;
	backbuf_t   backbuf;
	info_t     *info;
	info_key_t *name_key;
	int         packet;
	recorder_t *recorder;
	sizebuf_t   datagram;
	byte        datagram_buf[MAX_DATAGRAM];
} sv_qtv_t;

#define MAX_PROXIES 2

static sv_qtv_t proxies[MAX_PROXIES];

static sv_qtv_t *
alloc_proxy (void)
{
	int         i;

	for (i = 0; i < MAX_PROXIES; i++) {
		if (!proxies[i].info)
			return proxies;
	}
	return 0;
}

static void
drop_proxy (sv_qtv_t *proxy)
{
	SV_Printf ("dropped %s\n", proxy->name_key->value);
	if (proxy->recorder) {
		SVR_RemoveUser (proxy->recorder);
		proxy->recorder = 0;
	}
	MSG_WriteByte (&proxy->netchan.message, qtv_disconnect);
	Netchan_Transmit (&proxy->netchan, 0, 0);
	proxy->packet = 0;
	Info_Destroy (proxy->info);
	proxy->info = 0;
}

static void
qtv_new_f (sv_qtv_t *proxy)
{
	const char *gamedir;
	int         pos, len;
	byte       *buf;

	gamedir = Info_ValueForKey (svs.info, "*gamedir");
	if (!gamedir[0])
		gamedir = "qw";

	MSG_WriteByte (&proxy->netchan.message, qtv_packet);
	pos = proxy->netchan.message.cursize;
	MSG_WriteShort (&proxy->netchan.message, qtv_p_signon);
	MSG_WriteByte (&proxy->netchan.message, svc_serverdata);
	MSG_WriteLong (&proxy->netchan.message, PROTOCOL_VERSION);
	MSG_WriteLong (&proxy->netchan.message, svs.spawncount);
	MSG_WriteString (&proxy->netchan.message, gamedir);
	SV_WriteWorldVars (&proxy->netchan);
	len = proxy->netchan.message.cursize - pos - 2;
	buf = proxy->netchan.message.data + pos;
	buf[0] = len & 0xff;
	buf[1] |= (len >> 8) & 0x0f;
}

static void
qtv_soundlist_f (sv_qtv_t *proxy)
{
	int         n;
	int         pos, len;
	byte       *buf;

	if (atoi (Cmd_Argv (1)) != svs.spawncount) {
		qtv_new_f (proxy);
		return;
	}

	n = atoi (Cmd_Argv (2));
	if (n >= MAX_SOUNDS) {
		qtv_new_f (proxy);
		return;
	}
	MSG_WriteByte (&proxy->netchan.message, qtv_packet);
	pos = proxy->netchan.message.cursize;
	MSG_WriteShort (&proxy->netchan.message, qtv_p_signon);
	SV_WriteSoundlist (&proxy->netchan, n);
	len = proxy->netchan.message.cursize - pos - 2;
	buf = proxy->netchan.message.data + pos;
	buf[0] = len & 0xff;
	buf[1] |= (len >> 8) & 0x0f;
}

static void
qtv_modellist_f (sv_qtv_t *proxy)
{
	int         n;
	int         pos, len;
	byte       *buf;

	if (atoi (Cmd_Argv (1)) != svs.spawncount) {
		qtv_new_f (proxy);
		return;
	}

	n = atoi (Cmd_Argv (2));
	if (n >= MAX_SOUNDS) {
		qtv_new_f (proxy);
		return;
	}
	MSG_WriteByte (&proxy->netchan.message, qtv_packet);
	pos = proxy->netchan.message.cursize;
	MSG_WriteShort (&proxy->netchan.message, qtv_p_signon);
	SV_WriteModellist (&proxy->netchan, n);
	len = proxy->netchan.message.cursize - pos - 2;
	buf = proxy->netchan.message.data + pos;
	buf[0] = len & 0xff;
	buf[1] |= (len >> 8) & 0x0f;
}

static void
qtv_prespawn_f (sv_qtv_t *proxy)
{
	int         buf;
	int         size;
	const char *command;
	sizebuf_t  *msg;

	if (atoi (Cmd_Argv (1)) != svs.spawncount) {
		qtv_new_f (proxy);
		return;
	}

	buf = atoi (Cmd_Argv (2));
	if (buf >= sv.num_signon_buffers)
		buf = 0;

	if (buf == sv.num_signon_buffers - 1)
		command = va ("cmd spawn %i 0\n", svs.spawncount);
	else
		command = va ("cmd prespawn %i %i\n", svs.spawncount, buf + 1);
	size = 5 + sv.signon_buffer_size[buf] + 1 + strlen (command) + 2;

	msg = MSG_ReliableCheckBlock (&proxy->backbuf, size);

	MSG_WriteByte (msg, qtv_packet);
	MSG_WriteShort (msg, qtv_p_signon | sv.signon_buffer_size[buf]);
	SZ_Write (msg, sv.signon_buffers[buf], sv.signon_buffer_size[buf]);

	MSG_WriteByte (msg, qtv_stringcmd);
	MSG_WriteString (msg, command);
}

static void
qtv_spawn_f (sv_qtv_t *proxy)
{
	int         pos, len;
	byte       *buf;

	if (atoi (Cmd_Argv (1)) != svs.spawncount) {
		qtv_new_f (proxy);
		return;
	}
	MSG_WriteByte (&proxy->netchan.message, qtv_packet);
	pos = proxy->netchan.message.cursize;
	MSG_WriteShort (&proxy->netchan.message, qtv_p_signon);
	SV_WriteSpawn1 (&proxy->backbuf, 0);
	SV_WriteSpawn2 (&proxy->backbuf);
	len = proxy->netchan.message.cursize - pos - 2;
	buf = proxy->netchan.message.data + pos;
	buf[0] = len & 0xff;
	buf[1] |= (len >> 8) & 0x0f;
	MSG_ReliableWrite_Begin (&proxy->backbuf, qtv_stringcmd, 8);
	MSG_ReliableWrite_String (&proxy->backbuf, "skins\n");
}

static void
qtv_write (void *r, sizebuf_t *msg, int reliable)
{
	sv_qtv_t   *proxy = (sv_qtv_t *) r;
	sizebuf_t  *buf;
	int         size;

	if (reliable) {
		if (msg->cursize) {
			size = msg->cursize + 3;
			buf = MSG_ReliableCheckBlock (&proxy->backbuf, size);

			MSG_WriteByte (buf, qtv_packet);
			MSG_WriteShort (buf, qtv_p_reliable | msg->cursize);
			SZ_Write (buf, msg->data, msg->cursize);
		}
	} else {
		MSG_WriteByte (&proxy->datagram, qtv_packet);
		MSG_WriteShort (&proxy->datagram, qtv_p_unreliable | msg->cursize);
		SZ_Write (&proxy->datagram, msg->data, msg->cursize);
	}
}

static int
qtv_frame (void *r)
{
	sv_qtv_t   *proxy = (sv_qtv_t *) r;
	SV_Printf ("A proxy->packet: %d\n", proxy->packet);
	return proxy->packet;
}

static void
qtv_finish (void *r, sizebuf_t *msg)
{
}

static void
qtv_begin_f (sv_qtv_t *proxy)
{
	if (atoi (Cmd_Argv (1)) != svs.spawncount) {
		qtv_new_f (proxy);
		return;
	}
	proxy->recorder = SVR_AddUser (qtv_write, qtv_frame, qtv_finish, 0, proxy);
}

typedef struct {
	const char *name;
	void      (*func) (sv_qtv_t *proxy);
	unsigned    no_redirect:1;
} qcmd_t;

qcmd_t qcmds[] = {
	{"drop",		drop_proxy},
	{"new",			qtv_new_f},
	{"soundlist",	qtv_soundlist_f},
	{"modellist",	qtv_modellist_f},
	{"prespawn",	qtv_prespawn_f},
	{"spawn",		qtv_spawn_f},
	{"begin",		qtv_begin_f},

	{0,				0},
};

static void
qtv_command (sv_qtv_t *proxy, const char *s)
{
	qcmd_t     *c;
	const char *name;

	COM_TokenizeString (s, sv_args);
	cmd_args = sv_args;
	name = Cmd_Argv (0);

	for (c = qcmds; c->name; c++)
		if (strcmp (c->name, name) == 0)
			break;
	if (!c->name) {
		SV_Printf ("Bad QTV command: %s\n", sv_args->argv[0]->str);
		return;
	}
	c->func (proxy);
}

static void
qtv_parse (sv_qtv_t *proxy)
{
	int         c;

	proxy->packet = 1;
	while (1) {
		if (net_message->badread) {
			SV_Printf ("SV_ReadClientMessage: badread\n");
			drop_proxy (proxy);
			return;
		}

		c = MSG_ReadByte (net_message);
		if (c == -1)
			return;

		switch (c) {
			default:
				SV_Printf ("SV_ReadClientMessage: unknown command char\n");
				drop_proxy (proxy);
				return;
			case qtv_nop:
				break;

			case qtv_stringcmd:
				qtv_command (proxy, MSG_ReadString (net_message));
				break;

			case qtv_delta:
				MSG_ReadByte (net_message);
				break;
		}
	}
}

static void
qtv_reliable_send (sv_qtv_t *proxy)
{
	int         pos, len;
	byte       *buf;

	MSG_WriteByte (&proxy->netchan.message, qtv_packet);
	pos = proxy->netchan.message.cursize;
	MSG_WriteShort (&proxy->netchan.message, qtv_p_reliable);
	MSG_Reliable_Send (&proxy->backbuf);
	len = proxy->netchan.message.cursize - pos - 2;
	buf = proxy->netchan.message.data + pos;
	buf[0] = len & 0xff;
	buf[1] |= (len >> 8) & 0x0f;
}

void
SV_qtvConnect (int qport, info_t *info)
{
	sv_qtv_t   *proxy;

	SV_Printf ("QTV proxy connection: %d %s\n", Cmd_Argc (), Cmd_Args (1));
	SV_Printf ("qport: %d\n", qport);

	if (!(proxy = alloc_proxy ())) {
		SV_Printf ("%s:full proxy connect\n", NET_AdrToString (net_from));
		Netchan_OutOfBandPrint (net_from, "%c\nserver is full\n\n", A2C_PRINT);
		return;
	}
	proxy->info = info;
	while (!(proxy->name_key = Hash_Find (proxy->info->tab, "name")))
		Info_SetValueForKey (proxy->info, "name", "\"unnamed proxy\"", 1);

	Netchan_Setup (&proxy->netchan, net_from, qport, NC_READ_QPORT);
	proxy->backbuf.netchan = &proxy->netchan;
	proxy->backbuf.name = proxy->name_key->value;
	proxy->datagram.data = proxy->datagram_buf;
	proxy->datagram.maxsize = sizeof (proxy->datagram_buf);

	Netchan_OutOfBandPrint (net_from, "%c", S2C_CONNECTION);
}

int
SV_qtvPacket (int qport)
{
	int         i;

	for (i = 0; i < MAX_PROXIES; i++) {
		if (!proxies[i].info)
			continue;
		if (!NET_CompareBaseAdr (net_from, proxies[i].netchan.remote_address))
			continue;
		if (proxies[i].netchan.qport != qport)
			continue;
		if (proxies[i].netchan.remote_address.port != net_from.port) {
			Con_DPrintf ("SV_ReadPackets: fixing up a translated port\n");
			proxies[i].netchan.remote_address.port = net_from.port;
		}
		if (Netchan_Process (&proxies[i].netchan)) {
			// this is a valid, sequenced packet, so process it
			svs.stats.packets++;
		}
		qtv_parse (&proxies[i]);
		return 1;
	}
	return 0;
}

void
SV_qtvCheckTimeouts (void)
{
	int         i;
	float       droptime;
	sv_qtv_t   *proxy;

	droptime = realtime - timeout->value;

	for (i = 0; i < MAX_PROXIES; i++) {
		proxy = proxies + i;
		if (proxy->info) {
			if (proxy->netchan.last_received < droptime) {
				SV_Printf ("%s timed out\n", proxy->name_key->value);
				drop_proxy (proxy);
			}
		}
	}
}

void
SV_qtvSendMessages (void)
{
	int         i;
	sv_qtv_t   *proxy;

	for (i = 0; i < MAX_PROXIES; i++) {
		proxy = proxies + i;
		if (!proxy->info)
			continue;
		if (!proxy->packet)
			continue;
		if (proxy->backbuf.num_backbuf)
			qtv_reliable_send (proxy);
		if (proxy->netchan.message.overflowed) {
			SZ_Clear (&proxy->netchan.message);
			drop_proxy (proxy);
			continue;
		}
		if (proxy->datagram.cursize) {
			Netchan_Transmit (&proxy->netchan, proxy->datagram.cursize,
							  proxy->datagram.data);
			SZ_Clear (&proxy->datagram);
		} else {
			Netchan_Transmit (&proxy->netchan, 0, 0);
		}
		SV_Printf ("B proxy->packet: %d\n", proxy->packet);
		proxy->packet = 0;
	}
}

void
SV_qtvFinalMessage (const char *message)
{
	sv_qtv_t   *proxy;
	int         i;
	int         pos, len;
	byte       *buf;

	SZ_Clear (net_message->message);
	MSG_WriteByte (net_message->message, qtv_packet);
	pos = net_message->message->cursize;
	MSG_WriteShort (net_message->message, qtv_p_print);
	MSG_WriteByte (net_message->message, PRINT_HIGH);
	MSG_WriteString (net_message->message, message);
	len = net_message->message->cursize - pos - 2;
	buf = net_message->message->data + pos;
	buf[0] = len & 0xff;
	buf[1] |= (len >> 8) & 0x0f;
	MSG_WriteByte (net_message->message, qtv_disconnect);

	for (i = 0; i < MAX_PROXIES; i++) {
		proxy = proxies + i;
		if (!proxy->info)
			continue;
		Netchan_Transmit (&proxy->netchan, net_message->message->cursize,
						  net_message->message->data);
	}
}
