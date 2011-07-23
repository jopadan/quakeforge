/*
	cl_demo.c

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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static __attribute__ ((used)) const char rcsid[] = 
	"$Id$";

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <time.h>
	
#include "QF/cbuf.h"
#include "QF/cmd.h"
#include "QF/cvar.h"
#include "QF/dstring.h"
#include "QF/msg.h"
#include "QF/qendian.h"
#include "QF/sys.h"
#include "QF/va.h"

#include "cl_cam.h"
#include "cl_demo.h"
#include "cl_ents.h"
#include "cl_main.h"
#include "client.h"
#include "compat.h"
#include "host.h"
#include "qw/pmove.h"

typedef struct {
	int         frames;
	double      time;
	double      fps;
} td_stats_t;

int     demo_timeframes_isactive;
int     demo_timeframes_index;
int     demotime_cached;
float   nextdemotime;
char    demoname[1024];
double *demo_timeframes_array;
#define CL_TIMEFRAMES_ARRAYBLOCK 4096

int     timedemo_count;
int     timedemo_runs;
td_stats_t *timedemo_data;


static void CL_FinishTimeDemo (void);
static void CL_TimeFrames_DumpLog (void);
static void CL_TimeFrames_AddTimestamp (void);
static void CL_TimeFrames_Reset (void);

cvar_t     *demo_speed;
cvar_t     *demo_gzip;
cvar_t     *demo_quit;
cvar_t     *demo_timeframes;

/*
	DEMO CODE

	When a demo is playing back, all NET_SendMessages are skipped, and
	NET_GetMessages are read from the demo file.

	Whenever cl.time gets past the last received message, another message is
	read from the demo file.
*/


/*
	CL_StopPlayback

	Called when a demo file runs out, or the user starts a game
*/
void
CL_StopPlayback (void)
{
	if (!cls.demoplayback)
		return;

	Qclose (cls.demofile);
	cls.demofile = NULL;
	CL_SetState (ca_disconnected);
	cls.demoplayback = 0;
	cls.demoplayback2 = 0;
	demotime_cached = 0;
	net_blocksend = 0;

	if (cls.timedemo)
		CL_FinishTimeDemo ();
}

/*
	CL_WriteDemoCmd

	Writes the current user cmd
*/
void
CL_WriteDemoCmd (usercmd_t *pcmd)
{
	byte		c;
	float		fl;
	int			i;
	usercmd_t	cmd;

	fl = LittleFloat ((float) realtime);
	Qwrite (cls.demofile, &fl, sizeof (fl));

	c = dem_cmd;
	Qwrite (cls.demofile, &c, sizeof (c));

	// correct for byte order, bytes don't matter
	cmd = *pcmd;

	for (i = 0; i < 3; i++)
		cmd.angles[i] = LittleFloat (cmd.angles[i]);
	cmd.forwardmove = LittleShort (cmd.forwardmove);
	cmd.sidemove = LittleShort (cmd.sidemove);
	cmd.upmove = LittleShort (cmd.upmove);

	Qwrite (cls.demofile, &cmd, sizeof (cmd));

	for (i = 0; i < 3; i++) {
		fl = LittleFloat (cl.viewangles[i]);
		Qwrite (cls.demofile, &fl, 4);
	}

	Qflush (cls.demofile);
}

/*
	CL_WriteDemoMessage

	Dumps the current net message, prefixed by the length and view angles
*/
static void
CL_WriteDemoMessage (sizebuf_t *msg)
{
	byte		c;
	float		fl;
	int			len;

	if (!cls.demorecording)
		return;

	fl = LittleFloat ((float) realtime);
	Qwrite (cls.demofile, &fl, sizeof (fl));

	c = dem_read;
	Qwrite (cls.demofile, &c, sizeof (c));

	len = LittleLong (msg->cursize);
	Qwrite (cls.demofile, &len, 4);
	Qwrite (cls.demofile, msg->data, msg->cursize);

	Qflush (cls.demofile);
}

#if 0
static const char *dem_names[] = {
	"dem_cmd",
	"dem_read",
	"dem_set",
	"dem_multiple",
	"dem_single",
	"dem_stats",
	"dem_all",
	"dem_invalid",
};
#endif

static qboolean
CL_GetDemoMessage (void)
{
	byte		c, newtime;
	float		demotime, f;
	static float cached_demotime;
	static byte cached_newtime;
	int			r, i, j, tracknum;
	usercmd_t  *pcmd;

	if (!cls.demoplayback2)
		nextdemotime = realtime;
	if (realtime + 1.0 < nextdemotime)
		realtime = nextdemotime - 1.0;

nextdemomessage:
	// read the time from the packet
	newtime = 0;
	if (demotime_cached) {
		demotime = cached_demotime;
		newtime = cached_newtime;
		demotime_cached = 0;
	} else {
		if (cls.demoplayback2) {
			Qread (cls.demofile, &newtime, sizeof (newtime));
			demotime = cls.basetime + (cls.prevtime + newtime) * 0.001;
		} else {
			Qread (cls.demofile, &demotime, sizeof (demotime));
			demotime = LittleFloat (demotime);
			if (!nextdemotime)
				realtime = nextdemotime = demotime;
		}
	}

	// decide if it is time to grab the next message        
	if (cls.timedemo) {
		if (cls.td_lastframe < 0)
			cls.td_lastframe = demotime;
		else if (demotime > cls.td_lastframe) {
			cls.td_lastframe = demotime;
			// rewind back to time
			demotime_cached = 1;
			cached_demotime = demotime;
			cached_newtime = newtime;
			return 0;					// already read this frame's message
		}
		if (!cls.td_starttime && cls.state == ca_active) {
			cls.td_starttime = Sys_DoubleTime ();
			cls.td_startframe = host_framecount;
		}
		realtime = demotime;			// warp
	} else if (!cl.paused && cls.state >= ca_onserver) {
		// always grab until fully connected
		if (!cls.demoplayback2 && realtime + 1.0 < demotime) {
			// too far back
			realtime = demotime - 1.0;
			// rewind back to time
			demotime_cached = 1;
			cached_demotime = demotime;
			cached_newtime = newtime;
			return 0;
		} else if (realtime < demotime) {
			// rewind back to time
			demotime_cached = 1;
			cached_demotime = demotime;
			cached_newtime = newtime;
			return 0;					// don't need another message yet
		}
	} else
		realtime = demotime;			// we're warping

	if (realtime - nextdemotime > 0.0001) {
		if (nextdemotime != demotime) {
			if (cls.demoplayback2) {
				cls.netchan.incoming_sequence++;
				cls.netchan.incoming_acknowledged++;
				cls.netchan.frame_latency = 0;
			}
		}
	}
	nextdemotime = demotime;

	cls.prevtime += newtime;

	if (cls.state < ca_demostart)
		Host_Error ("CL_GetDemoMessage: cls.state != ca_active");

	// get the msg type
	Qread (cls.demofile, &c, sizeof (c));

	switch (c & 7) {
		case dem_cmd:
			// user sent input
			net_message->message->cursize = -1;
			i = cls.netchan.outgoing_sequence & UPDATE_MASK;
			pcmd = &cl.frames[i].cmd;
			r = Qread (cls.demofile, pcmd, sizeof (*pcmd));
			if (r != sizeof (*pcmd)) {
				CL_StopPlayback ();
				return 0;
			}
			// byte order stuff
			for (j = 0; j < 3; j++)
				pcmd->angles[j] = LittleFloat (pcmd->angles[j]);
			pcmd->forwardmove = LittleShort (pcmd->forwardmove);
			pcmd->sidemove = LittleShort (pcmd->sidemove);
			pcmd->upmove = LittleShort (pcmd->upmove);
			cl.frames[i].senttime = demotime;
			cl.frames[i].receivedtime = -1;	// we haven't gotten a reply yet
			cls.netchan.outgoing_sequence++;
			for (i = 0; i < 3; i++) {
				Qread (cls.demofile, &f, 4);
				cl.viewangles[i] = LittleFloat (f);
			}
			break;

		case dem_read:
readit:
			// get the next message
			Qread (cls.demofile, &net_message->message->cursize, 4);
			net_message->message->cursize = LittleLong
				(net_message->message->cursize);
			if (net_message->message->cursize > MAX_MSGLEN + 8) //+8 for header
				Host_Error ("Demo message > MAX_MSGLEN + 8: %d/%d",
							net_message->message->cursize, MAX_MSGLEN + 8);
			r = Qread (cls.demofile, net_message->message->data,
					   net_message->message->cursize);
			if (r != net_message->message->cursize) {
				CL_StopPlayback ();
				return 0;
			}

			if (cls.demoplayback2) {
				tracknum = Cam_TrackNum ();

				if (cls.lasttype == dem_multiple) {
					if (tracknum == -1)
						goto nextdemomessage;
					if (!(cls.lastto & (1 << tracknum)))
						goto nextdemomessage;
				} else if (cls.lasttype == dem_single) {
					if (tracknum == -1 || cls.lastto != spec_track)
						goto nextdemomessage;
				}
			}
			break;

		case dem_set:
			Qread (cls.demofile, &i, 4);
			cls.netchan.outgoing_sequence = LittleLong (i);
			Qread (cls.demofile, &i, 4);
			cls.netchan.incoming_sequence = LittleLong (i);
			if (cls.demoplayback2) {
				cls.netchan.incoming_acknowledged =
					cls.netchan.incoming_sequence;
				goto nextdemomessage;
			}
			break;

		case dem_multiple:
			r = Qread (cls.demofile, &i, 4);
			if (r != 4) {
				CL_StopPlayback ();
				return 0;
			}
			cls.lastto = LittleLong (i);
			cls.lasttype = dem_multiple;
			goto readit;

		case dem_single:
			cls.lastto = c >> 3;
			cls.lasttype = dem_single;
			goto readit;

		case dem_stats:
			cls.lastto = c >> 3;
			cls.lasttype = dem_stats;
			goto readit;

		case dem_all:
			cls.lastto = 0;
			cls.lasttype = dem_all;
			goto readit;

		default:
			Sys_Printf ("Corrupted demo.\n");
			CL_StopPlayback ();
			return 0;
	}

	return 1;
}

/*
	CL_GetMessage

	Handles recording and playback of demos, on top of NET_ code
*/
qboolean
CL_GetMessage (void)
{
	if (cls.demoplayback) {
		qboolean    ret = CL_GetDemoMessage ();

		if (!ret && demo_timeframes_isactive && cls.td_starttime) {
			CL_TimeFrames_AddTimestamp ();
		}
		return ret;
	}

	if (!NET_GetPacket ())
		return false;

	if (net_packetlog->int_val)
		Log_Incoming_Packet (net_message->message->data,
							 net_message->message->cursize, 1);

	CL_WriteDemoMessage (net_message->message);

	return true;
}

/*
	CL_Stop_f

	stop recording a demo
*/
void
CL_Stop_f (void)
{
	if (!cls.demorecording) {
		Sys_Printf ("Not recording a demo.\n");
		return;
	}
	// write a disconnect message to the demo file
	SZ_Clear (net_message->message);
	MSG_WriteLong (net_message->message, -1);  // -1 sequence means out of band
	MSG_WriteByte (net_message->message, svc_disconnect);
	MSG_WriteString (net_message->message, "EndOfDemo");
	CL_WriteDemoMessage (net_message->message);

	// finish up
	Qclose (cls.demofile);
	cls.demofile = NULL;
	cls.demorecording = false;
	Sys_Printf ("Completed demo\n");
}

/*
	CL_WriteDemoMessage

	Dumps the current net message, prefixed by the length and view angles
*/
static void
CL_WriteRecordDemoMessage (sizebuf_t *msg, int seq)
{
	byte		c;
	float		fl;
	int			len, i;

	if (!cls.demorecording)
		return;

	fl = LittleFloat ((float) realtime);
	Qwrite (cls.demofile, &fl, sizeof (fl));

	c = dem_read;
	Qwrite (cls.demofile, &c, sizeof (c));

	len = LittleLong (msg->cursize + 8);
	Qwrite (cls.demofile, &len, 4);

	i = LittleLong (seq);
	Qwrite (cls.demofile, &i, 4);
	Qwrite (cls.demofile, &i, 4);

	Qwrite (cls.demofile, msg->data, msg->cursize);

	Qflush (cls.demofile);
}

static void
CL_WriteSetDemoMessage (void)
{
	byte		c;
	float		fl;
	int			len;

	if (!cls.demorecording)
		return;

	fl = LittleFloat ((float) realtime);
	Qwrite (cls.demofile, &fl, sizeof (fl));

	c = dem_set;
	Qwrite (cls.demofile, &c, sizeof (c));

	len = LittleLong (cls.netchan.outgoing_sequence);
	Qwrite (cls.demofile, &len, 4);
	len = LittleLong (cls.netchan.incoming_sequence);
	Qwrite (cls.demofile, &len, 4);

	Qflush (cls.demofile);
}

void
CL_Record (const char *argv1)
{
	byte        buf_data[MAX_MSGLEN + 10];	// + 10 for header
	dstring_t  *name;
	char       *s;
	char        timestring[20];

	int         n, i, j;
	size_t      k;
	int         seq = 1;
	entity_t   *ent;
	entity_state_t *es, blankes;
	player_info_t *player;
	sizebuf_t   buf;
	time_t      tim;

	if (!argv1) {
		char       *mapname;

		// Get time to a useable format
		time (&tim);
		strftime (timestring, 19, "%Y-%m-%d-%H-%M", localtime (&tim));

		// the leading path-name is to be removed from cl.worldmodel->name
		mapname = strdup (QFS_SkipPath (cl.worldmodel->name));

		// the map name is cut off after any "." because this would prevent
		// ".qwd" from being appended

		for (k = 0; k <= strlen (mapname); k++)
			if (mapname[k] == '.')
				mapname[k] = '\0';

		name = dstring_new ();
		dsprintf (name, "%s/%s-%s", qfs_gamedir->dir.def, timestring, mapname);
		free (mapname);
	} else {
		name = dstring_new ();
		dsprintf (name, "%s/%s", qfs_gamedir->dir.def, argv1);
	}

	// open the demo file
#ifdef HAVE_ZLIB
	if (demo_gzip->int_val) {
		QFS_DefaultExtension (name, ".qwd.gz");
		cls.demofile = QFS_WOpen (name->str, demo_gzip->int_val);
	} else
#endif
	{
		QFS_DefaultExtension (name, ".qwd");
		cls.demofile = QFS_WOpen (name->str, 0);
	}
	if (!cls.demofile) {
		Sys_Printf ("ERROR: couldn't open.\n");
		dstring_delete (name);
		return;
	}

	Sys_Printf ("recording to %s.\n", name->str);
	dstring_delete (name);
	cls.demorecording = true;

/*-------------------------------------------------*/

	// serverdata
	// send the info about the new client to all connected clients
	memset (&buf, 0, sizeof (buf));
	buf.data = buf_data;
	buf.maxsize = sizeof (buf_data);

	// send the serverdata
	MSG_WriteByte (&buf, svc_serverdata);
	MSG_WriteLong (&buf, PROTOCOL_VERSION);
	MSG_WriteLong (&buf, cl.servercount);
	MSG_WriteString (&buf, qfs_gamedir->gamedir);

	if (cl.spectator)
		MSG_WriteByte (&buf, cl.playernum | 128);
	else
		MSG_WriteByte (&buf, cl.playernum);

	// send full levelname
	MSG_WriteString (&buf, cl.levelname);

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
							   Info_MakeString (cl.serverinfo, 0)));

	// flush packet
	CL_WriteRecordDemoMessage (&buf, seq++);
	SZ_Clear (&buf);

	// soundlist
	MSG_WriteByte (&buf, svc_soundlist);
	MSG_WriteByte (&buf, 0);

	n = 0;
	s = cl.sound_name[n + 1];
	while (*s) {
		MSG_WriteString (&buf, s);
		if (buf.cursize > MAX_MSGLEN / 2) {
			MSG_WriteByte (&buf, 0);
			MSG_WriteByte (&buf, n);
			CL_WriteRecordDemoMessage (&buf, seq++);
			SZ_Clear (&buf);
			MSG_WriteByte (&buf, svc_soundlist);
			MSG_WriteByte (&buf, n + 1);
		}
		n++;
		s = cl.sound_name[n + 1];
	}
	if (buf.cursize) {
		MSG_WriteByte (&buf, 0);
		MSG_WriteByte (&buf, 0);
		CL_WriteRecordDemoMessage (&buf, seq++);
		SZ_Clear (&buf);
	}
	// modellist
	MSG_WriteByte (&buf, svc_modellist);
	MSG_WriteByte (&buf, 0);

	n = 0;
	s = cl.model_name[n + 1];
	while (*s) {
		MSG_WriteString (&buf, s);
		if (buf.cursize > MAX_MSGLEN / 2) {
			MSG_WriteByte (&buf, 0);
			MSG_WriteByte (&buf, n);
			CL_WriteRecordDemoMessage (&buf, seq++);
			SZ_Clear (&buf);
			MSG_WriteByte (&buf, svc_modellist);
			MSG_WriteByte (&buf, n + 1);
		}
		n++;
		s = cl.model_name[n + 1];
	}
	if (buf.cursize) {
		MSG_WriteByte (&buf, 0);
		MSG_WriteByte (&buf, 0);
		CL_WriteRecordDemoMessage (&buf, seq++);
		SZ_Clear (&buf);
	}
	// spawnstatic
	for (ent = cl_static_entities; ent; ent = ent->unext) {
		MSG_WriteByte (&buf, svc_spawnstatic);

		for (j = 1; j < MAX_MODELS; j++)
			if (ent->model == cl.model_precache[j])
				break;
		if (j == MAX_MODELS)
			MSG_WriteByte (&buf, 0);
		else
			MSG_WriteByte (&buf, j);

		MSG_WriteByte (&buf, ent->frame);
		MSG_WriteByte (&buf, 0);
		MSG_WriteByte (&buf, ent->skinnum);
		MSG_WriteCoordAngleV (&buf, ent->origin, ent->angles);

		if (buf.cursize > MAX_MSGLEN / 2) {
			CL_WriteRecordDemoMessage (&buf, seq++);
			SZ_Clear (&buf);
		}
	}

	// spawnstaticsound
	// static sounds are skipped in demos, life is hard

	// baselines
	memset (&blankes, 0, sizeof (blankes));
	for (i = 0; i < MAX_EDICTS; i++) {
		es = cl_baselines + i;

		if (memcmp (es, &blankes, sizeof (blankes))) {
			MSG_WriteByte (&buf, svc_spawnbaseline);
			MSG_WriteShort (&buf, i);

			MSG_WriteByte (&buf, es->modelindex);
			MSG_WriteByte (&buf, es->frame);
			MSG_WriteByte (&buf, es->colormap);
			MSG_WriteByte (&buf, es->skinnum);
			MSG_WriteCoordAngleV (&buf, es->origin, es->angles);

			if (buf.cursize > MAX_MSGLEN / 2) {
				CL_WriteRecordDemoMessage (&buf, seq++);
				SZ_Clear (&buf);
			}
		}
	}

	MSG_WriteByte (&buf, svc_stufftext);
	MSG_WriteString (&buf, va ("cmd spawn %i 0\n", cl.servercount));

	if (buf.cursize) {
		CL_WriteRecordDemoMessage (&buf, seq++);
		SZ_Clear (&buf);
	}
	// send current status of all other players

	for (i = 0; i < MAX_CLIENTS; i++) {
		player = cl.players + i;
		if (!player->userinfo)
			continue;

		MSG_WriteByte (&buf, svc_updatefrags);
		MSG_WriteByte (&buf, i);
		MSG_WriteShort (&buf, player->frags);

		MSG_WriteByte (&buf, svc_updateping);
		MSG_WriteByte (&buf, i);
		MSG_WriteShort (&buf, player->ping);

		MSG_WriteByte (&buf, svc_updatepl);
		MSG_WriteByte (&buf, i);
		MSG_WriteByte (&buf, player->pl);

		MSG_WriteByte (&buf, svc_updateentertime);
		MSG_WriteByte (&buf, i);
		MSG_WriteFloat (&buf, realtime - player->entertime);

		MSG_WriteByte (&buf, svc_updateuserinfo);
		MSG_WriteByte (&buf, i);
		MSG_WriteLong (&buf, player->userid);
		MSG_WriteString (&buf, Info_MakeString (player->userinfo, 0));

		if (buf.cursize > MAX_MSGLEN / 2) {
			CL_WriteRecordDemoMessage (&buf, seq++);
			SZ_Clear (&buf);
		}
	}

	// send all current light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++) {
		MSG_WriteByte (&buf, svc_lightstyle);
		MSG_WriteByte (&buf, (char) i);
		MSG_WriteString (&buf, r_lightstyle[i].map);
	}

	for (i = 0; i < MAX_CL_STATS; i++) {
		MSG_WriteByte (&buf, svc_updatestatlong);
		MSG_WriteByte (&buf, i);
		MSG_WriteLong (&buf, cl.stats[i]);
		if (buf.cursize > MAX_MSGLEN / 2) {
			CL_WriteRecordDemoMessage (&buf, seq++);
			SZ_Clear (&buf);
		}
	}

#if 0
	MSG_WriteByte (&buf, svc_updatestatlong);
	MSG_WriteByte (&buf, STAT_TOTALMONSTERS);
	MSG_WriteLong (&buf, cl.stats[STAT_TOTALMONSTERS]);

	MSG_WriteByte (&buf, svc_updatestatlong);
	MSG_WriteByte (&buf, STAT_SECRETS);
	MSG_WriteLong (&buf, cl.stats[STAT_SECRETS]);

	MSG_WriteByte (&buf, svc_updatestatlong);
	MSG_WriteByte (&buf, STAT_MONSTERS);
	MSG_WriteLong (&buf, cl.stats[STAT_MONSTERS]);
#endif

	// get the client to check and download skins
	// when that is completed, a begin command will be issued
	MSG_WriteByte (&buf, svc_stufftext);
	MSG_WriteString (&buf, va ("skins\n"));

	CL_WriteRecordDemoMessage (&buf, seq++);

	CL_WriteSetDemoMessage ();

	// done
}

/*
	CL_Record_f

	record <demoname> <server>
*/
void
CL_Record_f (void)
{
	if (Cmd_Argc () > 2) {
		// we use a demo name like year-month-day-hours-minutes-mapname.qwd
		// if there is no argument
		Sys_Printf ("record [demoname]\n");
		return;
	}

	if (cls.demoplayback || cls.state != ca_active) {
		Sys_Printf ("You must be connected to record.\n");
		return;
	}

	if (cls.demorecording)
		CL_Stop_f ();
	if (Cmd_Argc () == 2)
		CL_Record (Cmd_Argv (1));
	else
		CL_Record (0);
}

/*
	CL_ReRecord_f

	record <demoname>
*/
void
CL_ReRecord_f (void)
{
	dstring_t  *name;
	int			c;

	c = Cmd_Argc ();
	if (c != 2) {
		Sys_Printf ("rerecord <demoname>\n");
		return;
	}

	if (!cls.servername || !cls.servername->str) {
		Sys_Printf ("No server to which to reconnect...\n");
		return;
	}

	if (cls.demorecording)
		CL_Stop_f ();

	name = dstring_newstr ();
	dsprintf (name, "%s/%s", qfs_gamedir->dir.def, Cmd_Argv (1));

	// open the demo file
	QFS_DefaultExtension (name, ".qwd");

	cls.demofile = QFS_WOpen (name->str, 0);
	if (!cls.demofile) {
		Sys_Printf ("ERROR: couldn't open.\n");
	} else {
		Sys_Printf ("recording to %s.\n", name->str);
		cls.demorecording = true;

		CL_Disconnect ();
		CL_BeginServerConnect ();
	}
	dstring_delete (name);
}

static void
CL_StartDemo (void)
{
	dstring_t  *name = dstring_newstr ();

	// open the demo file
	dstring_copystr (name, demoname);
	QFS_DefaultExtension (name, ".qwd");

	Sys_Printf ("Playing demo from %s.\n", name->str);
	QFS_FOpenFile (name->str, &cls.demofile);
	if (!cls.demofile) {
		Sys_Printf ("ERROR: couldn't open.\n");
		cls.demonum = -1;				// stop demo loop
		dstring_delete (name);
		return;
	}

	cls.demoplayback = true;
	net_blocksend = 1;
	if (strequal (QFS_FileExtension (name->str), ".mvd")) {
		cls.demoplayback2 = true;
		Sys_Printf ("mvd\n");
	} else {
		Sys_Printf ("qwd\n");
	}
	CL_SetState (ca_demostart);
	Netchan_Setup (&cls.netchan, net_from, 0, NC_QPORT_SEND);
	realtime = 0;
	cls.findtrack = true;
	cls.lasttype = 0;
	cls.lastto = 0;
	cls.prevtime = 0;
	cls.basetime = 0;
	demotime_cached = 0;
	nextdemotime = 0;
	CL_ClearPredict ();

	dstring_delete (name);
}

/*
	CL_PlayDemo_f

	play [demoname]
*/
void
CL_PlayDemo_f (void)
{
	if (Cmd_Argc () != 2) {
		Sys_Printf ("play <demoname> : plays a demo\n");
		return;
	}
	timedemo_runs = timedemo_count = 1;	// make sure looped timedemos stop
	// disconnect from server
	CL_Disconnect ();

	strncpy (demoname, Cmd_Argv (1), sizeof (demoname));
	CL_StartDemo ();
}

static void
CL_StartTimeDemo (void)
{
	CL_StartDemo ();

	if (cls.state != ca_demostart)
		return;

	// cls.td_starttime will be grabbed at the second frame of the demo, so
	// all the loading time doesn't get counted

	cls.timedemo = true;
	cls.td_starttime = 0;
	cls.td_startframe = host_framecount;
	cls.td_lastframe = -1;				// get a new message this frame

	CL_TimeFrames_Reset ();
	if (demo_timeframes->int_val)
		demo_timeframes_isactive = 1;
}

static inline double
sqr (double x)
{
	return x * x;
}

static void
CL_FinishTimeDemo (void)
{
	float		time;
	int			frames;

	cls.timedemo = false;

	// the first frame didn't count
	frames = (host_framecount - cls.td_startframe) - 1;
	time = Sys_DoubleTime () - cls.td_starttime;
	if (!time)
		time = 1;
	Sys_Printf ("%i frame%s %.4g seconds %.4g fps\n", frames,
				frames == 1 ? "" : "s", time, frames / time);

	CL_TimeFrames_DumpLog ();
	demo_timeframes_isactive = 0;

	timedemo_count--;
	timedemo_data[timedemo_count].frames = frames;
	timedemo_data[timedemo_count].time = time;
	timedemo_data[timedemo_count].fps = frames / time;
	if (timedemo_count > 0) {
		CL_StartTimeDemo ();
	} else {
		if (--timedemo_runs > 0) {
			double      average = 0;
			double      variance = 0;
			double      min, max;
			int         i;

			min = max = timedemo_data[0].fps;
			for (i = 0; i < timedemo_runs; i++) {
				average += timedemo_data[i].fps;
				min = min (min, timedemo_data[i].fps);
				max = max (max, timedemo_data[i].fps);
			}
			average /= timedemo_runs;
			for (i = 0; i < timedemo_runs; i++)
				variance += sqr (timedemo_data[i].fps - average);
			variance /= timedemo_runs;
			Sys_Printf ("timedemo stats for %d runs:\n", timedemo_runs);
			Sys_Printf ("  average fps: %.3f\n", average);
			Sys_Printf ("  min/max fps: %.3f/%.3f\n", min, max);
			Sys_Printf ("std deviation: %.3f fps\n", sqrt (variance));
		}
		free (timedemo_data);
		timedemo_data = 0;
		if (demo_quit->int_val)
			Cbuf_InsertText (cl_cbuf, "quit\n");
	}
}

/*
	CL_TimeDemo_f

	timedemo [demoname]
*/
void
CL_TimeDemo_f (void)
{
	if (Cmd_Argc () < 2 || Cmd_Argc () > 3) {
		Sys_Printf ("timedemo <demoname> [count]: gets demo speeds\n");
		return;
	}
	timedemo_runs = timedemo_count = 1;	// make sure looped timedemos stop
	// disconnect from server
	CL_Disconnect ();

	if (Cmd_Argc () == 3) {
		timedemo_count = atoi (Cmd_Argv (2));
	} else {
		timedemo_count = 1;
	}
	timedemo_runs = timedemo_count = max (timedemo_count, 1);
	if (timedemo_data)
		free (timedemo_data);
	timedemo_data = calloc (timedemo_runs, sizeof (td_stats_t));
	strncpy (demoname, Cmd_Argv (1), sizeof (demoname));
	CL_StartTimeDemo ();
}

void
CL_Demo_Init (void)
{
	demo_timeframes_isactive = 0;
	demo_timeframes_index = 0;
	demo_timeframes_array = NULL;

	demo_gzip = Cvar_Get ("demo_gzip", "0", CVAR_ARCHIVE, NULL,
						  "Compress demos using gzip. 0 = none, 1 = least "
						  "compression, 9 = most compression. Compressed "
						  " demos (1-9) will have .gz appended to the name");
	demo_speed = Cvar_Get ("demo_speed", "1.0", CVAR_NONE, NULL,
						   "adjust demo playback speed. 1.0 = normal, "
						   "< 1 slow-mo, > 1 timelapse");
	demo_quit = Cvar_Get ("demo_quit", "0", CVAR_NONE, NULL,
						  "automaticly quit after a timedemo has finished");
	demo_timeframes = Cvar_Get ("demo_timeframes", "0", CVAR_NONE, NULL,
								"write timestamps for every frame");
}

static void
CL_TimeFrames_Reset (void)
{
	demo_timeframes_index = 0;
	free (demo_timeframes_array);
	demo_timeframes_array = NULL;
}

static void
CL_TimeFrames_AddTimestamp (void)
{
	if (!(demo_timeframes_index % CL_TIMEFRAMES_ARRAYBLOCK))
		demo_timeframes_array = realloc
			(demo_timeframes_array, sizeof (demo_timeframes_array[0]) *
			 ((demo_timeframes_index / CL_TIMEFRAMES_ARRAYBLOCK) + 1) *
			 CL_TIMEFRAMES_ARRAYBLOCK);
	if (demo_timeframes_array == NULL)
		Sys_Error ("Unable to allocate timeframes buffer");
	demo_timeframes_array[demo_timeframes_index] = Sys_DoubleTime ();
	demo_timeframes_index++;
}

static void
CL_TimeFrames_DumpLog (void)
{
	const char *filename = "timeframes.txt";
	int			i;
	long		frame;
	QFile	   *outputfile;

	if (demo_timeframes_isactive == 0)
		return;

	Sys_Printf ("Dumping Timed Frames log: %s\n", filename);
	outputfile = QFS_Open (filename, "w");
	if (!outputfile) {
		Sys_Printf ("Could not open: %s\n", filename);
		return;
	}
	for (i = 1; i < demo_timeframes_index; i++) {
		frame = (demo_timeframes_array[i] - demo_timeframes_array[i - 1]) * 1e6;
		Qprintf (outputfile, "%09ld\n", frame);
	}
	Qclose (outputfile);
}
