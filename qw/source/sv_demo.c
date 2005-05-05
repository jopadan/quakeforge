/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the included (GNU.txt) GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static __attribute__ ((unused)) const char rcsid[] = 
	"$Id$";

#ifdef HAVE_STRING_H
# include "string.h"
#endif
#ifdef HAVE_STRINGS_H
# include "strings.h"
#endif
#ifdef HAVE_UNISTD_H
# include "unistd.h"
#endif
#include <sys/time.h>
#include <time.h>

#include "QF/cmd.h"
#include "QF/cvar.h"
#include "QF/dstring.h"
#include "QF/info.h"
#include "QF/msg.h"
#include "QF/qargs.h"
#include "QF/qendian.h"
#include "QF/quakefs.h"
#include "QF/sys.h"
#include "QF/va.h"

#include "compat.h"
#include "qw/pmove.h"
#include "server.h"
#include "sv_demo.h"
#include "sv_progs.h"
#include "sv_recorder.h"

static QFile   *demo_file;
static byte    *demo_mfile;
static qboolean demo_disk;
static dstring_t *demo_name;		// filename of mvd
static dstring_t *demo_text;		// filename of description file
static void    *demo_dest;
static double   demo_time;

static recorder_t *recorder;

#define MIN_DEMO_MEMORY 0x100000
#define USECACHE (sv_demoUseCache->int_val && svs.demomemsize)
#define DWRITE(a,b,d) dwrite((QFile *) d, a, b)

static int  demo_max_size;
static int  demo_size;

cvar_t         *sv_demofps;
cvar_t         *sv_demoPings;
cvar_t         *sv_demoMaxSize;
static cvar_t  *sv_demoUseCache;
static cvar_t  *sv_demoCacheSize;
static cvar_t  *sv_demoMaxDirSize;
static cvar_t  *sv_demoDir;
static cvar_t  *sv_demoNoVis;
static cvar_t  *sv_demoPrefix;
static cvar_t  *sv_demoSuffix;
static cvar_t  *sv_onrecordfinish;
static cvar_t  *sv_ondemoremove;
static cvar_t  *sv_demotxt;
static cvar_t  *serverdemo;

static int    (*dwrite) (QFile * file, const void *buf, int count);

#define HEADER  ((int) &((header_t *) 0)->data)

static int
memwrite (QFile *_mem, const void *buffer, int size)
{
	byte      **mem = (byte **) _mem;

	memcpy (*mem, buffer, size);
	*mem += size;

	return size;
}

static void
demo_write (void *unused, sizebuf_t *msg, int unused2)
{
	DWRITE (msg->data, msg->cursize, demo_dest);
}

static int
demo_frame (void *unused)
{
	double      min_fps;

	if (!sv_demofps->value)
		min_fps = 20.0;
	else
		min_fps = sv_demofps->value;
	min_fps = max (4, min_fps);
	if (sv.time - demo_time < 1.0 / min_fps)
		return 0;
	demo_time = sv.time;
	return 1;
}

static void
demo_finish (void *unused, sizebuf_t *msg)
{
	// write a disconnect message to the demo file
	MSG_WriteByte (msg, svc_disconnect);
	MSG_WriteString (msg, "EndOfDemo");
	recorder = 0;
}

/*
	SV_Stop

	stop recording a demo
*/
void
SV_Stop (int reason)
{
	if (!recorder) {
		Con_Printf ("Not recording a demo.\n");
		return;
	}

	if (reason == 2) {
		// stop and remove
		if (demo_disk)
			Qclose (demo_file);

		QFS_Remove (demo_name->str);
		QFS_Remove (demo_text->str);

		demo_file = NULL;
		SVR_RemoveUser (recorder);

		SV_BroadcastPrintf (PRINT_CHAT,
							"Server recording canceled, demo removed\n");

		Cvar_Set (serverdemo, "");

		return;
	}

	// clearup to be sure message will fit

	SVR_RemoveUser (recorder);

	Qclose (demo_file);
	demo_file = NULL;
	recorder = 0;
	if (!reason)
		SV_BroadcastPrintf (PRINT_CHAT, "Server recording completed\n");
	else
		SV_BroadcastPrintf (PRINT_CHAT, "Server recording stoped\n"
							"Max demo size exceeded\n");
/*
	if (sv_onrecordfinish->string[0]) {
		extern redirect_t sv_redirected;
		int         old = sv_redirected;
		char        path[MAX_OSPATH];
		char       *p;

		if ((p = strstr (sv_onrecordfinish->string, " ")) != NULL)
			*p = 0;						// strip parameters

		strcpy (path, demo_name->str);
		strcpy (path + strlen (demo_name->str) - 3, "txt");

		sv_redirected = RD_NONE;		// onrecord script is called always
										// from the console
		Cmd_TokenizeString (va ("script %s \"%s\" \"%s\" \"%s\" %s",
								sv_onrecordfinish->string, demo.path->str,
								serverdemo->string,
								path, p != NULL ? p + 1 : ""));
		if (p)
			*p = ' ';
		SV_Script_f ();

		sv_redirected = old;
	}
*/
	Cvar_Set (serverdemo, "");
}

static void
SV_Stop_f (void)
{
	SV_Stop (0);
}

/*
	SV_Cancel_f

	Stops recording, and removes the demo
*/
static void
SV_Cancel_f (void)
{
	SV_Stop (2);
}

static qboolean
SV_InitRecord (void)
{
	if (!USECACHE) {
		dwrite = &Qwrite;
		demo_dest = demo_file;
		demo_disk = true;
	} else {
		dwrite = &memwrite;
		demo_mfile = svs.demomem;
		demo_dest = &demo_mfile;
	}

	demo_size = 0;

	return true;
}

/*
	SV_WriteRecordDemoMessage

	Dumps the current net message, prefixed by the length and view angles
*/

static void
SV_WriteRecordDemoMessage (sizebuf_t *msg)
{
	int         len;
	byte        c;

	c = 0;
	/*demo.size +=*/ DWRITE (&c, sizeof (c), demo_dest);

	c = dem_read;
	/*demo.size +=*/ DWRITE (&c, sizeof (c), demo_dest);

	len = LittleLong (msg->cursize);
	/*demo.size +=*/ DWRITE (&len, 4, demo_dest);

	/*demo.size +=*/ DWRITE (msg->data, msg->cursize, demo_dest);

	if (demo_disk)
		Qflush (demo_file);
}

static void
SV_WriteSetDemoMessage (void)
{
	int         len;
	byte        c;

	c = 0;
	/*demo.size +=*/ DWRITE (&c, sizeof (c), demo_dest);

	c = dem_set;
	/*demo.size +=*/ DWRITE (&c, sizeof (c), demo_dest);


	len = LittleLong (0);
	/*demo.size +=*/ DWRITE (&len, 4, demo_dest);
	len = LittleLong (0);
	/*demo.size +=*/ DWRITE (&len, 4, demo_dest);

	if (demo_disk)
		Qflush (demo_file);
}

static const char *
SV_PrintTeams (void)
{
	const char *teams[MAX_CLIENTS];
	char       *p;
	int         i, j, numcl = 0, numt = 0;
	client_t   *clients[MAX_CLIENTS];
	const char *team;
	static dstring_t *buffer;

	if (!buffer)
		buffer = dstring_new ();

	// count teams and players
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (svs.clients[i].state != cs_spawned
			&& svs.clients[i].state != cs_server)
			continue;
		if (svs.clients[i].spectator)
			continue;

		team = Info_ValueForKey (svs.clients[i].userinfo, "team");
		clients[numcl++] = &svs.clients[i];

		for (j = 0; j < numt; j++)
			if (!strcmp (team, teams[j]))
				break;
		if (j != numt)
			continue;

		teams[numt++] = team;
	}

	// create output

	if (numcl == 2) {					// duel
		dsprintf (buffer, "team1 %s\nteam2 %s\n", clients[0]->name,
				  clients[1]->name);
	} else if (!teamplay->int_val) {	// ffa
		dsprintf (buffer, "players:\n");
		for (i = 0; i < numcl; i++)
			dasprintf (buffer, "  %s\n", clients[i]->name);
	} else {							// teamplay
		for (j = 0; j < numt; j++) {
			dasprintf (buffer, "team %s:\n", teams[j]);
			for (i = 0; i < numcl; i++) {
				team = Info_ValueForKey (svs.clients[i].userinfo, "team");
				if (!strcmp (team, teams[j]))
					dasprintf (buffer, "  %s\n", clients[i]->name);
			}
		}
	}

	if (!numcl)
		return "\n";
	for (p = buffer->str; *p; p++)
		*p = sys_char_map[(byte) * p];
	return buffer->str;
}

static int
make_info_string_filter (const char *key)
{
	return *key == '_' || !Info_FilterForKey (key, client_info_filters);
}

static void
SV_Record (char *name)
{
	sizebuf_t   buf;
	char        buf_data[MAX_MSGLEN];
	int         n, i;
	const char *info;

	client_t   *player;
	const char *gamedir, *s;

	demo_file = QFS_Open (name, "wb");
	if (!demo_file) {
		Con_Printf ("ERROR: couldn't open %s\n", name);
		return;
	}

	SV_InitRecord ();

	s = name + strlen (name);
	while (s > name && *s != '/')
		s--;
	dstring_copystr (demo_name, s + (*s == '/'));

	SV_BroadcastPrintf (PRINT_CHAT, "Server started recording (%s):\n%s\n",
						demo_disk ? "disk" : "memory", demo_name->str);
	Cvar_Set (serverdemo, demo_name->str);

	dstring_copystr (demo_text, name);
	strcpy (demo_text->str + strlen (demo_text->str) - 3, "txt");

	if (sv_demotxt->int_val) {
		QFile      *f;

		f = QFS_Open (demo_text->str, "w+t");
		if (f != NULL) {
			char        date[20];
			time_t      tim;

			time (&tim);
			strftime (date, sizeof (date), "%Y-%m-%d-%H-%M", localtime (&tim));
			Qprintf (f, "date %s\nmap %s\nteamplay %d\ndeathmatch %d\n"
					 "timelimit %d\n%s",
					 date, sv.name, teamplay->int_val,
					 deathmatch->int_val, timelimit->int_val, 
					 SV_PrintTeams ());
			Qclose (f);
		}
	} else
		QFS_Remove (demo_text->str);

	recorder = SVR_AddUser (demo_write, demo_frame, demo_finish, 1, 0);
	demo_time = sv.time;

/*-------------------------------------------------*/

	// serverdata
	// send the info about the new client to all connected clients
	memset (&buf, 0, sizeof (buf));
	buf.data = buf_data;
	buf.maxsize = sizeof (buf_data);

	// send the serverdata

	gamedir = Info_ValueForKey (svs.info, "*gamedir");
	if (!gamedir[0])
		gamedir = "qw";

	MSG_WriteByte (&buf, svc_serverdata);
	MSG_WriteLong (&buf, PROTOCOL_VERSION);
	MSG_WriteLong (&buf, svs.spawncount);
	MSG_WriteString (&buf, gamedir);


	MSG_WriteFloat (&buf, sv.time);

	// send full levelname
	MSG_WriteString (&buf, PR_GetString (&sv_pr_state,
										 SVstring (sv.edicts, message)));

	// send the movevars
	MSG_WriteFloat (&buf, movevars.gravity);
	MSG_WriteFloat (&buf, movevars.stopspeed);
	MSG_WriteFloat (&buf, movevars.maxspeed);
	MSG_WriteFloat (&buf, movevars.spectatormaxspeed);
	MSG_WriteFloat (&buf, movevars.accelerate);
	MSG_WriteFloat (&buf, movevars.airaccelerate);
	MSG_WriteFloat (&buf, movevars.wateraccelerate);
	MSG_WriteFloat (&buf, movevars.friction);
	MSG_WriteFloat (&buf, movevars.waterfriction);
	MSG_WriteFloat (&buf, movevars.entgravity);

	// send music
	MSG_WriteByte (&buf, svc_cdtrack);
	MSG_WriteByte (&buf, 0);			// none in demos

	// send server info string
	MSG_WriteByte (&buf, svc_stufftext);
	MSG_WriteString (&buf, va ("fullserverinfo \"%s\"\n",
							   Info_MakeString (svs.info, 0)));

	// flush packet
	SV_WriteRecordDemoMessage (&buf);
	SZ_Clear (&buf);

	// soundlist
	MSG_WriteByte (&buf, svc_soundlist);
	MSG_WriteByte (&buf, 0);

	n = 0;
	s = sv.sound_precache[n + 1];
	while (s) {
		MSG_WriteString (&buf, s);
		if (buf.cursize > MAX_MSGLEN / 2) {
			MSG_WriteByte (&buf, 0);
			MSG_WriteByte (&buf, n);
			SV_WriteRecordDemoMessage (&buf);
			SZ_Clear (&buf);
			MSG_WriteByte (&buf, svc_soundlist);
			MSG_WriteByte (&buf, n + 1);
		}
		n++;
		s = sv.sound_precache[n + 1];
	}

	if (buf.cursize) {
		MSG_WriteByte (&buf, 0);
		MSG_WriteByte (&buf, 0);
		SV_WriteRecordDemoMessage (&buf);
		SZ_Clear (&buf);
	}
	// modellist
	MSG_WriteByte (&buf, svc_modellist);
	MSG_WriteByte (&buf, 0);

	n = 0;
	s = sv.model_precache[n + 1];
	while (s) {
		MSG_WriteString (&buf, s);
		if (buf.cursize > MAX_MSGLEN / 2) {
			MSG_WriteByte (&buf, 0);
			MSG_WriteByte (&buf, n);
			SV_WriteRecordDemoMessage (&buf);
			SZ_Clear (&buf);
			MSG_WriteByte (&buf, svc_modellist);
			MSG_WriteByte (&buf, n + 1);
		}
		n++;
		s = sv.model_precache[n + 1];
	}
	if (buf.cursize) {
		MSG_WriteByte (&buf, 0);
		MSG_WriteByte (&buf, 0);
		SV_WriteRecordDemoMessage (&buf);
		SZ_Clear (&buf);
	}
	// prespawn

	for (n = 0; n < sv.num_signon_buffers; n++) {
		SZ_Write (&buf, sv.signon_buffers[n], sv.signon_buffer_size[n]);

		if (buf.cursize > MAX_MSGLEN / 2) {
			SV_WriteRecordDemoMessage (&buf);
			SZ_Clear (&buf);
		}
	}

	MSG_WriteByte (&buf, svc_stufftext);
	MSG_WriteString (&buf, va ("cmd spawn %i 0\n", svs.spawncount));

	if (buf.cursize) {
		SV_WriteRecordDemoMessage (&buf);
		SZ_Clear (&buf);
	}
	// send current status of all other players

	for (i = 0; i < MAX_CLIENTS; i++) {
		player = svs.clients + i;

		MSG_WriteByte (&buf, svc_updatefrags);
		MSG_WriteByte (&buf, i);
		MSG_WriteShort (&buf, player->old_frags);

		MSG_WriteByte (&buf, svc_updateping);
		MSG_WriteByte (&buf, i);
		MSG_WriteShort (&buf, SV_CalcPing (player));

		MSG_WriteByte (&buf, svc_updatepl);
		MSG_WriteByte (&buf, i);
		MSG_WriteByte (&buf, player->lossage);

		MSG_WriteByte (&buf, svc_updateentertime);
		MSG_WriteByte (&buf, i);
		MSG_WriteFloat (&buf, realtime - player->connection_started);

		info = player->userinfo ? Info_MakeString (player->userinfo,
												   make_info_string_filter)
								: "";

		MSG_WriteByte (&buf, svc_updateuserinfo);
		MSG_WriteByte (&buf, i);
		MSG_WriteLong (&buf, player->userid);
		MSG_WriteString (&buf, info);

		if (buf.cursize > MAX_MSGLEN / 2) {
			SV_WriteRecordDemoMessage (&buf);
			SZ_Clear (&buf);
		}
	}

	// send all current light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++) {
		MSG_WriteByte (&buf, svc_lightstyle);
		MSG_WriteByte (&buf, (char) i);
		MSG_WriteString (&buf, sv.lightstyles[i]);
	}

	// get the client to check and download skins
	// when that is completed, a begin command will be issued
	MSG_WriteByte (&buf, svc_stufftext);
	MSG_WriteString (&buf, va ("skins\n"));

	SV_WriteRecordDemoMessage (&buf);

	SV_WriteSetDemoMessage ();
	// done
}

/*
	SV_CleanName

	Cleans the demo name, removes restricted chars, makes name lowercase
*/

static char *
SV_CleanName (const unsigned char *name)
{
	static char *text;
	static size_t text_len;
	char       *out, c;

	if (text_len < strlen (name)) {
		text_len = (strlen (name) + 1023) & ~1023;
		text = realloc (text, text_len);
	}

	out = text;
	do {
		c = sys_char_map[*name++];
		if (c != '_')
			*out++ = c;
	} while (c);

	return text;
}

/*
	SV_Record_f

	record <demoname>
*/
static void
SV_Record_f (void)
{
	dstring_t  *name = dstring_newstr ();

	if (Cmd_Argc () != 2) {
		Con_Printf ("record <demoname>\n");
		return;
	}

	if (sv.state != ss_active) {
		Con_Printf ("Not active yet.\n");
		return;
	}

	if (recorder)
		SV_Stop (0);

	dsprintf (name, "%s/%s/%s%s%s", qfs_gamedir->dir.def, sv_demoDir->string,
			  sv_demoPrefix->string, SV_CleanName (Cmd_Argv (1)),
			  sv_demoSuffix->string);

	// open the demo file
	name->size += 4;
	dstring_adjust (name);
	QFS_DefaultExtension (name->str, ".mvd");

	SV_Record (name->str);

	dstring_delete (name);
}

/*
	SV_EasyRecord_f

	easyrecord [demoname]
*/

static int
Dem_CountPlayers (void)
{
	int         i, count;

	count = 0;
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (svs.clients[i].name[0] && !svs.clients[i].spectator)
			count++;
	}

	return count;
}

static const char *
Dem_Team (int num)
{
	int         i;
	static const char *lastteam[2];
	qboolean    first = true;
	client_t   *client;
	static int  index = 0;
	const char *team;

	index = 1 - index;

	for (i = 0, client = svs.clients; num && i < MAX_CLIENTS; i++, client++) {
		if (!client->name[0] || client->spectator)
			continue;

		team = Info_ValueForKey (svs.clients[i].userinfo, "team");
		if (first || strcmp (lastteam[index], team)) {
			first = false;
			num--;
			lastteam[index] = team;
		}
	}

	if (num)
		return "";

	return lastteam[index];
}

static const char *
Dem_PlayerName (int num)
{
	int         i;
	client_t   *client;

	for (i = 0, client = svs.clients; i < MAX_CLIENTS; i++, client++) {
		if (!client->name[0] || client->spectator)
			continue;

		if (!--num)
			return client->name;
	}

	return "";
}

static void
SV_EasyRecord_f (void)
{
	dstring_t  *name = dstring_newstr ();
	dstring_t  *name2 = dstring_newstr ();
	int         i;

	if (Cmd_Argc () > 2) {
		Con_Printf ("easyrecord [demoname]\n");
		return;
	}

	if (recorder)
		SV_Stop (0);

	if (Cmd_Argc () == 2)
		dsprintf (name, "%s", Cmd_Argv (1));
	else {
		// guess game type and write demo name
		i = Dem_CountPlayers ();
		if (teamplay->int_val && i > 2) {
			// Teamplay
			dsprintf (name, "team_%s_vs_%s_%s",
					  Dem_Team (1), Dem_Team (2), sv.name);
		} else {
			if (i == 2) {
				// Duel
				dsprintf (name, "duel_%s_vs_%s_%s",
						  Dem_PlayerName (1), Dem_PlayerName (2), sv.name);
			} else {
				// FFA
				dsprintf (name, "ffa_%s (%d)", sv.name, i);
			}
		}
	}

	// Make sure the filename doesn't contain illegal characters
	dsprintf (name2, "%s/%s%s%s%s%s", 
			  qfs_gamedir->dir.def, sv_demoDir->string,
			  sv_demoDir->string[0] ? "/" : "",
			  sv_demoPrefix->string, SV_CleanName (name->str),
			  sv_demoSuffix->string);

	if (QFS_NextFilename (name, name2->str, ".mvd"))
		SV_Record (name->str);

	dstring_delete (name);
	dstring_delete (name2);
}

void
Demo_Init (void)
{
	int         p, size = MIN_DEMO_MEMORY;

	p = COM_CheckParm ("-democache");
	if (p) {
		if (p < com_argc - 1)
			size = atoi (com_argv[p + 1]) * 1024;
		else
			Sys_Error ("Memory_Init: you must specify a size in KB after "
					   "-democache");
	}

	if (size < MIN_DEMO_MEMORY) {
		Con_Printf ("Minimum memory size for demo cache is %dk\n",
					MIN_DEMO_MEMORY / 1024);
		size = MIN_DEMO_MEMORY;
	}

	demo_name = dstring_newstr ();
	demo_text = dstring_newstr ();

	svs.demomem = Hunk_AllocName (size, "demo");
	svs.demomemsize = size;
	demo_max_size = size - 0x80000;

	serverdemo = Cvar_Get ("serverdemo", "", CVAR_SERVERINFO, Cvar_Info,
						   "FIXME");
	sv_demofps = Cvar_Get ("sv_demofps", "20", CVAR_NONE, 0, "FIXME");
	sv_demoPings = Cvar_Get ("sv_demoPings", "3", CVAR_NONE, 0, "FIXME");
	sv_demoNoVis = Cvar_Get ("sv_demoNoVis", "1", CVAR_NONE, 0, "FIXME");
	sv_demoUseCache = Cvar_Get ("sv_demoUseCache", "0", CVAR_NONE, 0, "FIXME");
	sv_demoCacheSize = Cvar_Get ("sv_demoCacheSize", va ("%d", size / 1024),
								 CVAR_ROM, 0, "FIXME");
	sv_demoMaxSize = Cvar_Get ("sv_demoMaxSize", "20480", CVAR_NONE, 0,
							   "FIXME");
	sv_demoMaxDirSize = Cvar_Get ("sv_demoMaxDirSize", "102400", CVAR_NONE, 0,
								  "FIXME");
	sv_demoDir = Cvar_Get ("sv_demoDir", "demos", CVAR_NONE, 0, "FIXME");
	sv_demoPrefix = Cvar_Get ("sv_demoPrefix", "", CVAR_NONE, 0, "FIXME");
	sv_demoSuffix = Cvar_Get ("sv_demoSuffix", "", CVAR_NONE, 0, "FIXME");
	sv_onrecordfinish = Cvar_Get ("sv_onrecordfinish", "", CVAR_NONE, 0, "FIXME");
	sv_ondemoremove = Cvar_Get ("sv_ondemoremove", "", CVAR_NONE, 0, "FIXME");
	sv_demotxt = Cvar_Get ("sv_demotxt", "1", CVAR_NONE, 0, "FIXME");

	Cmd_AddCommand ("record", SV_Record_f, "FIXME");
	Cmd_AddCommand ("easyrecord", SV_EasyRecord_f, "FIXME");
	Cmd_AddCommand ("stop", SV_Stop_f, "FIXME");
	Cmd_AddCommand ("cancel", SV_Cancel_f, "FIXME");
}
