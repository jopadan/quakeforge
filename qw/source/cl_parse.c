/*
	cl_parse.c

	parse a message received from the server

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
#ifdef HAVE_ERRNO_H
# include <errno.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "QF/cdaudio.h"
#include "QF/cmd.h"
#include "QF/console.h"
#include "QF/cvar.h"
#include "QF/hash.h"
#include "QF/msg.h"
#include "QF/screen.h"
#include "QF/sound.h"
#include "QF/sys.h"
#include "QF/teamplay.h"
#include "QF/va.h"
#include "QF/vfile.h"

#include "bothdefs.h"
#include "cl_ents.h"
#include "cl_input.h"
#include "cl_main.h"
#include "cl_parse.h"
#include "cl_skin.h"
#include "cl_tent.h"
#include "client.h"
#include "compat.h"
#include "host.h"
#include "net_svc.h"
#include "pmove.h"
#include "protocol.h"
#include "sbar.h"
#include "view.h"

char       *svc_strings[] = {
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_updatestat",
	"svc_version",				// [long] server version
	"svc_setview",				// [short] entity number
	"svc_sound",				// <see code>
	"svc_time",					// [float] server time
	"svc_print",				// [string] null terminated string
	"svc_stufftext",			// [string] stuffed into client's console
								// buffer the string should be \n terminated
	"svc_setangle",				// [vec3] set view angle to this absolute value

	"svc_serverdata",			// [long] version ...
	"svc_lightstyle",			// [byte] [string]
	"svc_updatename",			// [byte] [string]
	"svc_updatefrags",			// [byte] [short]
	"svc_clientdata",			// <shortbits + data>
	"svc_stopsound",			// <see code>
	"svc_updatecolors",			// [byte] [byte]
	"svc_particle",				// [vec3] <variable>
	"svc_damage",				// [byte] impact [byte] blood [vec3] from

	"svc_spawnstatic",
	"OBSOLETE svc_spawnbinary",
	"svc_spawnbaseline",

	"svc_temp_entity",			// <variable>
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_killedmonster",
	"svc_foundsecret",
	"svc_spawnstaticsound",
	"svc_intermission",
	"svc_finale",

	"svc_cdtrack",
	"svc_sellscreen",

	"svc_smallkick",
	"svc_bigkick",

	"svc_updateping",
	"svc_updateentertime",

	"svc_updatestatlong",
	"svc_muzzleflash",
	"svc_updateuserinfo",
	"svc_download",
	"svc_playerinfo",
	"svc_nails",
	"svc_choke",
	"svc_modellist",
	"svc_soundlist",
	"svc_packetentities",
	"svc_deltapacketentities",
	"svc_maxspeed",
	"svc_entgravity",

	"svc_setinfo",
	"svc_serverinfo",
	"svc_updatepl",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL"
};

int         oldparsecountmod;
int         parsecountmod;
double      parsecounttime;

int         viewentity;

int         cl_spikeindex, cl_playerindex, cl_flagindex;
int         cl_h_playerindex, cl_gib1index, cl_gib2index, cl_gib3index;

int         packet_latency[NET_TIMINGS];



int
CL_CalcNet (void)
{
	int         lost, a, i;
	frame_t    *frame;

	for (i = cls.netchan.outgoing_sequence - UPDATE_BACKUP + 1;
		 i <= cls.netchan.outgoing_sequence; i++) {
		frame = &cl.frames[i & UPDATE_MASK];
		if (frame->receivedtime == -1)
			packet_latency[i & NET_TIMINGSMASK] = 9999;	// dropped
		else if (frame->receivedtime == -2)
			packet_latency[i & NET_TIMINGSMASK] = 10000;	// choked
		else if (frame->invalid)
			packet_latency[i & NET_TIMINGSMASK] = 9998;	// invalid delta
		else
			packet_latency[i & NET_TIMINGSMASK] =
				(frame->receivedtime - frame->senttime) * 20;
	}

	lost = 0;
	for (a = 0; a < NET_TIMINGS; a++) {
		i = (cls.netchan.outgoing_sequence - a) & NET_TIMINGSMASK;
		if (packet_latency[i] == 9999)
			lost++;
	}
	return lost * 100 / NET_TIMINGS;
}

/*
	CL_CheckOrDownloadFile

	Returns true if the file exists, otherwise it attempts
	to start a download from the server.
*/
qboolean
CL_CheckOrDownloadFile (const char *filename)
{
	VFile      *f;

	if (strstr (filename, "..")) {
		Con_Printf ("Refusing to download a path with ..\n");
		return true;
	}

	if (!snd_initialized && strnequal ("sound/", filename, 6)) {
		// don't bother downloading sownds if we can't play them
		return true;
	}

	COM_FOpenFile (filename, &f);
	if (f) {							// it exists, no need to download
		Qclose (f);
		return true;
	}
	// ZOID - can't download when recording
	if (cls.demorecording) {
		Con_Printf ("Unable to download %s in record mode.\n",
					cls.downloadname);
		return true;
	}
	// ZOID - can't download when playback
	if (cls.demoplayback)
		return true;

	strcpy (cls.downloadname, filename);
	Con_Printf ("Downloading %s...\n", cls.downloadname);

	// download to a temp name, and only rename
	// to the real name when done, so if interrupted
	// a runt file wont be left
	COM_StripExtension (cls.downloadname, cls.downloadtempname);
	strncat (cls.downloadtempname, ".tmp",
			 sizeof (cls.downloadtempname) - strlen (cls.downloadtempname));

	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	MSG_WriteString (&cls.netchan.message,
					 va ("download %s", cls.downloadname));

	cls.downloadnumber++;

	return false;
}

void
Model_NextDownload (void)
{
	char       *s;
	int         i;

	if (cls.downloadnumber == 0) {
		Con_Printf ("Checking models...\n");
		CL_UpdateScreen (realtime);
		cls.downloadnumber = 1;
	}

	cls.downloadtype = dl_model;
	for (; cl.model_name[cls.downloadnumber][0]; cls.downloadnumber++) {
		s = cl.model_name[cls.downloadnumber];
		if (s[0] == '*')
			continue;					// inline brush model
		if (!CL_CheckOrDownloadFile (s))
			return;						// started a download
	}

	if (!cls.demoplayback)
		Netchan_AckPacket (&cls.netchan);

	for (i = 1; i < MAX_MODELS; i++) {
		char *info_key = 0;

		if (!cl.model_name[i][0])
			break;

		cl.model_precache[i] = Mod_ForName (cl.model_name[i], false);

		if (!cl.model_precache[i]) {
			Con_Printf ("\nThe required model file '%s' could not be found or "
						"downloaded.\n\n", cl.model_name[i]);
			Con_Printf ("You may need to download or purchase a %s client "
						"pack in order to play on this server.\n\n",
						gamedirfile);
			CL_Disconnect ();
			return;
		}

		if (strequal (cl.model_name[i], "progs/player.mdl")
			&& cl.model_precache[i]->type == mod_alias)
			info_key = pmodel_name;
		if (strequal (cl.model_name[i], "progs/eyes.mdl")
			&& cl.model_precache[i]->type == mod_alias)
			info_key = emodel_name;

		if (info_key && cl_model_crcs->int_val) {
			aliashdr_t *ahdr = Cache_Get
				(&cl.model_precache[i]->cache);
			Info_SetValueForKey (cls.userinfo, info_key, va ("%d", ahdr->crc),
								 0);
			MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
			SZ_Print (&cls.netchan.message, va ("setinfo %s %d", info_key,
												ahdr->crc));
			Cache_Release (&cl.model_precache[i]->cache);
		}
	}

	// Something went wrong (probably in the server, probably a TF server)
	// We need to disconnect gracefully.
	if (!cl.model_precache[1]) {
		Con_Printf ("\nThe server has failed to provide the map name.\n\n");
		Con_Printf ("Disconnecting to prevent a crash.\n\n");
		CL_Disconnect ();
		return;
	}

	// all done
	cl.worldmodel = cl.model_precache[1];

	R_NewMap (cl.worldmodel, cl.model_precache, MAX_MODELS);
	Team_NewMap ();
	Hunk_Check ();						// make sure nothing is hurt

	// done with modellist, request first of static signon messages
	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	MSG_WriteString (&cls.netchan.message,
					 va (prespawn_name, cl.servercount,
						 cl.worldmodel->checksum2));
}

void
Sound_NextDownload (void)
{
	char       *s;
	int         i;

	if (cls.downloadnumber == 0) {
		Con_Printf ("Checking sounds...\n");
		CL_UpdateScreen (realtime);
		cls.downloadnumber = 1;
	}

	cls.downloadtype = dl_sound;
	for (; cl.sound_name[cls.downloadnumber][0];
		 cls.downloadnumber++) {
		s = cl.sound_name[cls.downloadnumber];
		if (!CL_CheckOrDownloadFile (va ("sound/%s", s)))
			return;						// started a download
	}

	if (!cls.demoplayback)
		Netchan_AckPacket (&cls.netchan);

	for (i = 1; i < MAX_SOUNDS; i++) {
		if (!cl.sound_name[i][0])
			break;
		cl.sound_precache[i] = S_PrecacheSound (cl.sound_name[i]);
	}

	// done with sounds, request models now
	memset (cl.model_precache, 0, sizeof (cl.model_precache));
	cl_playerindex = -1;
	cl_spikeindex = -1;
	cl_flagindex = -1;
	cl_h_playerindex = -1;
	cl_gib1index = cl_gib2index = cl_gib3index = -1;
	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	MSG_WriteString (&cls.netchan.message,
					 va (modellist_name, cl.servercount, 0));
}

void
CL_RequestNextDownload (void)
{
	switch (cls.downloadtype) {
		case dl_single:
			break;
		case dl_skin:
			Skin_NextDownload ();
			break;
		case dl_model:
			Model_NextDownload ();
			break;
		case dl_sound:
			Sound_NextDownload ();
			break;
		case dl_none:
		default:
			Con_DPrintf ("Unknown download type.\n");
	}
}

/*
	CL_ParseDownload

	A download message has been received from the server
*/
void
CL_ParseDownload (void)
{
	byte        name[1024];
	int         r;
	net_svc_download_t download;

	if (NET_SVC_Download_Parse (&download, net_message)) {
		Host_NetError ("CL_ParseDownload: Bad Read\n");
		return;
	}

	if (cls.demoplayback)
		return;							// not in demo playback

	if (download.size == -1) {
		Con_Printf ("File not found.\n");
		if (cls.download) {
			Con_Printf ("cls.download shouldn't have been set\n");
			Qclose (cls.download);
			cls.download = NULL;
		}
		CL_RequestNextDownload ();
		return;
	}

	if (download.size == -2) {
		// don't compare past the end of cls.downloadname due to gz support
		if (strncmp (download.name, cls.downloadname, strlen (cls.downloadname))
			|| strstr (download.name + strlen (cls.downloadname), "/")) {
			Con_Printf ("WARNING: server tried to give a strange new "
						"name: %s\n", download.name);
			CL_RequestNextDownload ();
			return;
		}
		if (cls.download) {
			Qclose (cls.download);
			unlink (cls.downloadname);
		}
		strncpy (cls.downloadname, download.name,
				 sizeof (cls.downloadname) - 1);
		Con_Printf ("downloading to %s\n", cls.downloadname);
		return;
	}

	if (download.size <= 0) {
		Host_NetError ("Bad download block, size %d", download.size);
		return;
	}

	// open the file if not opened yet
	if (!cls.download) {
		if (strncmp (cls.downloadtempname, "skins/", 6))
			snprintf (name, sizeof (name), "%s/%s", com_gamedir,
					  cls.downloadtempname);
		else
			snprintf (name, sizeof (name), "%s/%s/%s", fs_userpath->string,
					  fs_skinbase->string, cls.downloadtempname);

		COM_CreatePath (name);

		cls.download = Qopen (name, "wb");
		if (!cls.download) {
			Con_Printf ("Failed to open %s\n", cls.downloadtempname);
			CL_RequestNextDownload ();
			return;
		}
	}

	Qwrite (cls.download, download.data, download.size);

	if (download.percent != 100) {
		// change display routines by zoid
		// request next block
#if 0
		Con_Printf (".");
		if (10 * (download.percent / 10) != cls.downloadpercent) {
			cls.downloadpercent = 10 * (download.percent / 10);
			Con_Printf ("%i%%", cls.downloadpercent);
		}
#endif
		if (download.percent != cls.downloadpercent)
			VID_SetCaption (va ("Downloading %s %d%%", cls.downloadname,
								download.percent));
		cls.downloadpercent = download.percent;

		MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
		SZ_Print (&cls.netchan.message, "nextdl");
	} else {
		char        oldn[MAX_OSPATH];
		char        newn[MAX_OSPATH];

#if 0
		Con_Printf ("100%%\n");
#endif

		Qclose (cls.download);
		VID_SetCaption (va ("Connecting to %s", cls.servername));

		// rename the temp file to it's final name
		if (strcmp (cls.downloadtempname, cls.downloadname)) {
			if (strncmp (cls.downloadtempname, "skins/", 6)) {
				snprintf (oldn, sizeof (oldn), "%s/%s", com_gamedir,
						  cls.downloadtempname);
				snprintf (newn, sizeof (newn), "%s/%s", com_gamedir,
						  cls.downloadname);
			} else {
				snprintf (oldn, sizeof (oldn), "%s/%s/%s", fs_userpath->string,
						  fs_skinbase->string, cls.downloadtempname);
				snprintf (newn, sizeof (newn), "%s/%s/%s", fs_userpath->string,
						  fs_skinbase->string, cls.downloadname);
			}
			r = Qrename (oldn, newn);
			if (r)
				Con_Printf ("failed to rename, %s.\n", strerror (errno));
		}

		cls.download = NULL;
		cls.downloadpercent = 0;

		// get another file if needed

		CL_RequestNextDownload ();
	}
}

static byte *upload_data;
static int  upload_pos, upload_size;

void
CL_NextUpload (void)
{
	byte        buffer[1024];
	int         percent, size, r;

	if (!upload_data)
		return;

	r = upload_size - upload_pos;
	if (r > 768)
		r = 768;
	memcpy (buffer, upload_data + upload_pos, r);
	MSG_WriteByte (&cls.netchan.message, clc_upload);
	MSG_WriteShort (&cls.netchan.message, r);

	upload_pos += r;
	size = upload_size;
	if (!size)
		size = 1;
	percent = upload_pos * 100 / size;
	MSG_WriteByte (&cls.netchan.message, percent);
	SZ_Write (&cls.netchan.message, buffer, r);

	Con_DPrintf ("UPLOAD: %6d: %d written\n", upload_pos - r, r);

	if (upload_pos != upload_size)
		return;

	Con_Printf ("Upload completed\n");

	free (upload_data);
	upload_data = 0;
	upload_pos = upload_size = 0;
}

void
CL_StartUpload (byte * data, int size)
{
	if (cls.state < ca_onserver)
		return;							// gotta be connected

	// override
	if (upload_data)
		free (upload_data);

	Con_DPrintf ("Upload starting of %d...\n", size);

	upload_data = malloc (size);
	if (!upload_data)
		Sys_Error ("CL_StartUpload: Memory Allocation Failure\n");
	memcpy (upload_data, data, size);
	upload_size = size;
	upload_pos = 0;

	CL_NextUpload ();
}

qboolean
CL_IsUploading (void)
{
	if (upload_data)
		return true;
	return false;
}

void
CL_StopUpload (void)
{
	if (upload_data)
		free (upload_data);
	upload_data = NULL;
}

void
CL_ParsePrint (void)
{
	const char     *string;
	char			tmpstring[2048];
	net_svc_print_t	print;

	if (NET_SVC_Print_Parse (&print, net_message)) {
		Host_NetError ("CL_ParsePrint: Bad Read\n");
		return;
	}
	string = print.message;

	if (print.level == PRINT_CHAT) {
		// TODO: cl_nofake 2 -- accept fake messages from
		// teammates

		if (cl_nofake->int_val) {
			char		*c;
			strncpy (tmpstring, string, sizeof (tmpstring));
			tmpstring[sizeof (tmpstring) - 1] = 0;
			for (c = tmpstring; *c; c++)
				if (*c == '\r')
					*c = '#';
			string = tmpstring;
		}
		con_ormask = 128;
		S_LocalSound ("misc/talk.wav");
		Team_ParseChat(string);
	}
	Con_Printf ("%s", string);
	con_ormask = 0;
}

/* SERVER CONNECTING MESSAGES */

void Draw_ClearCache (void);
void CL_ClearBaselines (void);		// LordHavoc: BIG BUG-FIX!

void
CL_ParseServerData (void)
{
	char        fn[MAX_OSPATH];
	int         protover;
	qboolean    cflag = false;
	net_svc_serverdata_t serverdata;

	Con_DPrintf ("Serverdata packet received.\n");

	if (NET_SVC_ServerData_Parse (&serverdata, net_message)) {
		Host_NetError ("CL_ParseServerData: Bad Read\n");
		return;
	}

	// wipe the client_state_t struct
	CL_ClearState ();

	// parse protocol version number
	// allow 2.2 and 2.29 demos to play
	protover = serverdata.protocolversion;
	if (protover != PROTOCOL_VERSION &&
		!(cls.demoplayback
		  && (protover <= 26 && protover >= 28)))
			Host_NetError ("Server returned version %i, not %i\nYou probably "
						   "need to upgrade.\nCheck http://www.quakeworld.net/",
						   protover, PROTOCOL_VERSION);

	cl.servercount = serverdata.servercount;

	// game directory
	if (!strequal (gamedirfile, serverdata.gamedir)) {
		// save current config
		Host_WriteConfiguration ();
		cflag = true;
		Draw_ClearCache ();
	}

	COM_Gamedir (serverdata.gamedir);

	// ZOID--run the autoexec.cfg in the gamedir
	// if it exists
	if (cflag) {
		int         cmd_warncmd_val = cmd_warncmd->int_val;

		Cbuf_AddText ("cmd_warncmd 0\n");
		Cbuf_AddText ("exec config.cfg\n");
		Cbuf_AddText ("exec frontend.cfg\n");
		if (cl_autoexec->int_val) {
			Cbuf_AddText ("exec autoexec.cfg\n");
		}
		snprintf (fn, sizeof (fn), "cmd_warncmd %d\n", cmd_warncmd_val);
		Cbuf_AddText (fn);
	}

	// parse player slot
	cl.playernum = serverdata.playernum;
	cl.spectator = serverdata.spectator;
	if (cl.playernum >= MAX_CLIENTS) {
		Host_NetError ("CL_ParseServerData: playernum %d >= MAX_CLIENTS",
					   cl.playernum);
		return;
	}

// FIXME: evil hack so NQ and QW can share sound code
	cl.viewentity = cl.playernum + 1;
	viewentity = cl.playernum + 1;

	// get the full level name
	strncpy (cl.levelname, serverdata.levelname, sizeof (cl.levelname) - 1);

	// get the movevars
	memcpy (&movevars, &serverdata.movevars, sizeof (movevars));

	// seperate the printfs so the server message can have a color
	Con_Printf ("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36"
				"\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
	Con_Printf ("%c%s\n", 2, serverdata.levelname);

	// ask for the sound list next
	memset (cl.sound_name, 0, sizeof (cl.sound_name));
	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	MSG_WriteString (&cls.netchan.message,
					 va (soundlist_name, cl.servercount, 0));

	// now waiting for downloads, etc
	CL_SetState (ca_onserver);

	CL_ClearBaselines ();
}

// LordHavoc: BIG BUG-FIX!  Clear baselines each time it connects...
void
CL_ClearBaselines (void)
{
	int         i;

	memset (cl_baselines, 0, sizeof (cl_baselines));
	for (i = 0; i < MAX_EDICTS; i++) {
		cl_baselines[i].alpha = 255;
		cl_baselines[i].scale = 16;
		cl_baselines[i].glow_color = 254;
		cl_baselines[i].glow_size = 0;
		cl_baselines[i].colormod = 255;
	}
}

void
CL_ParseSoundlist (void)
{
	const char *str;
	int         numsounds, i;
	net_svc_soundlist_t soundlist;

	// precache sounds
	if (NET_SVC_Soundlist_Parse (&soundlist, net_message)) {
		Host_NetError ("CL_ParseSoundlist: Bad Read\n");
		return;
	}

	for (i = 0, numsounds = soundlist.startsound;; i++) {
		str = soundlist.sounds[i];
		if (!str[0])
			break;
		numsounds++;
		if (numsounds >= MAX_SOUNDS) {
			Host_NetError ("CL_ParseSoundlist: too many sounds");
			return;
		}
		strcpy (cl.sound_name[numsounds], str);
	}

	if (soundlist.nextsound) {
		MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
		MSG_WriteString (&cls.netchan.message,
						 va (soundlist_name, cl.servercount,
							 soundlist.nextsound));
		return;
	}

	cls.downloadnumber = 0;
	cls.downloadtype = dl_sound;
	Sound_NextDownload ();
}

void
CL_ParseModellist (void)
{
	int         nummodels, i;
	const char *str;
	net_svc_modellist_t modellist;

	// precache models and note certain default indexes
	if (NET_SVC_Modellist_Parse (&modellist, net_message)) {
		Host_NetError ("CL_ParseModellist: Bad Read\n");
		return;
	}

	for (i = 0, nummodels = modellist.startmodel;; i++) {
		str = modellist.models[i];
		if (!str[0])
			break;
		nummodels++;
		if (nummodels >= MAX_MODELS) {
			Host_NetError ("CL_ParseModellist: too many models");
			return;
		}
		strncpy (cl.model_name[nummodels], str, MAX_QPATH - 1);
		cl.model_name[nummodels][MAX_QPATH - 1] = '\0';

		if (!strcmp (str, "progs/spike.mdl"))
			cl_spikeindex = nummodels;
		else if (!strcmp (str, "progs/player.mdl"))
			cl_playerindex = nummodels;
		else if (!strcmp (str, "progs/flag.mdl"))
			cl_flagindex = nummodels;
		// for deadbodyfilter & gibfilter
		else if (!strcmp (str, "progs/h_player.mdl"))
			cl_h_playerindex = nummodels;
		else if (!strcmp (str, "progs/gib1.mdl"))
			cl_gib1index = nummodels;
		else if (!strcmp (str, "progs/gib2.mdl"))
			cl_gib2index = nummodels;
		else if (!strcmp (str, "progs/gib3.mdl"))
			cl_gib3index = nummodels;
	}

	if (modellist.nextmodel) {
		MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
		MSG_WriteString (&cls.netchan.message,
						 va (modellist_name, cl.servercount,
							 modellist.nextmodel));
		return;
	}

	cls.downloadnumber = 0;
	cls.downloadtype = dl_model;
	Model_NextDownload ();
}

void
CL_ParseSpawnBaseline ()
{
	entity_state_t *es;
	net_svc_spawnbaseline_t block;

	if (NET_SVC_SpawnBaseline_Parse (&block, net_message)) {
		Host_NetError ("CL_ParseSpawnBaseline: Bad Read\n");
		return;
	}

	if (block.num >= MAX_EDICTS) {
		Host_NetError ("CL_ParseSpawnBaseline: num %i >= MAX_EDICTS",
					   block.num);
		return;
	}
	if (block.modelindex >= MAX_MODELS) {
		Host_NetError ("CL_ParseSpawnBaseline: modelindex %i >= MAX_MODELS",
					   block.modelindex);
		return;
	}

	es = &cl_baselines[block.num];

	es->modelindex = block.modelindex;
	es->frame = block.frame;
	es->colormap = block.colormap;
	es->skinnum = block.skinnum;
	VectorCopy (block.origin, es->origin);
	VectorCopy (block.angles, es->angles);

	// LordHavoc: set up the baseline to account for new effects (alpha,
	// colormod, etc)
	es->alpha = 255;
	es->scale = 16;
	es->glow_color = 254;
	es->glow_size = 0;
	es->colormod = 255;
}

/*
	CL_ParseStatic

	Static entities are non-interactive world objects
	like torches
*/
void
CL_ParseStatic (void)
{
	entity_t   *ent;
	net_svc_spawnstatic_t block;

	if (NET_SVC_SpawnStatic_Parse (&block, net_message)) {
		Host_NetError ("CL_ParseStatic: Bad Read\n");
		return;
	}

	if (block.modelindex >= MAX_MODELS) {
		Host_NetError ("CL_ParseStatic: modelindex %i >= MAX_MODELS",
					   block.modelindex);
		return;
	}

	if (cl.num_statics >= MAX_STATIC_ENTITIES) {
		Host_NetError ("Too many static entities");
		return;
	}
	ent = &cl_static_entities[cl.num_statics++];
	CL_Init_Entity (ent);

	// copy it to the current state
	ent->model = cl.model_precache[block.modelindex];
	ent->frame = block.frame;
	ent->skinnum = block.skinnum;

	VectorCopy (block.origin, ent->origin);
	VectorCopy (block.angles, ent->angles);

	R_AddEfrags (ent);
}

void
CL_ParseStaticSound (void)
{
	net_svc_spawnstaticsound_t block;

	if (NET_SVC_SpawnStaticSound_Parse (&block, net_message)) {
		Host_NetError ("CL_ParseStaticSound: Bad Read\n");
		return;
	}

	S_StaticSound (cl.sound_precache[block.sound_num], block.position,
				   block.volume, block.attenuation);
}

/* ACTION MESSAGES */

void
CL_ParseStartSoundPacket (void)
{
	net_svc_sound_t sound;

	if (NET_SVC_Sound_Parse (&sound, net_message)) {
		Host_NetError ("CL_ParseStartSoundPacket: Bad Read\n");
		return;
	}

	if (sound.entity >= MAX_EDICTS) {
		Host_NetError ("CL_ParseStartSoundPacket: ent = %i", sound.entity);
		return;
	}
	if (sound.sound_num >= MAX_SOUNDS) {
		Host_NetError ("CL_ParseStartSoundPacket: sound_num = %i",
					   sound.sound_num);
		return;
	}

	S_StartSound (sound.entity, sound.channel,
				  cl.sound_precache[sound.sound_num], sound.position,
				  sound.volume, sound.attenuation);
}

/*
	CL_ParseClientdata

	Server information pertaining to this client only, sent every frame
*/
void
CL_ParseClientdata (void)
{
	float       latency;
	frame_t    *frame;
	int         i;

	// calculate simulated time of message
	oldparsecountmod = parsecountmod;

	i = cls.netchan.incoming_acknowledged;
	cl.parsecount = i;
	i &= UPDATE_MASK;
	parsecountmod = i;
	frame = &cl.frames[i];
	parsecounttime = cl.frames[i].senttime;

	frame->receivedtime = realtime;

	// calculate latency
	latency = frame->receivedtime - frame->senttime;

	if (latency < 0 || latency > 1.0) {
//		Con_Printf ("Odd latency: %5.2f\n", latency);
	} else {
		// drift the average latency towards the observed latency
		if (latency < cls.latency)
			cls.latency = latency;
		else
			cls.latency += 0.001;		// drift up, so correction is needed
	}
}

void
CL_ProcessUserInfo (int slot, player_info_t *player)
{
	char        skin[512];
	const char *s;

	s = Info_ValueForKey (player->userinfo, "skin");
	COM_StripExtension (s, skin);   // FIXME buffer overflow
	if (!strequal (s, skin))
		Info_SetValueForKey (player->userinfo, "skin", skin, 1);
	strncpy (player->name, Info_ValueForKey (player->userinfo, "name"),
			 sizeof (player->name) - 1);
	player->_topcolor = player->_bottomcolor = -1;
	player->topcolor = atoi (Info_ValueForKey (player->userinfo, "topcolor"));
	player->bottomcolor =
		atoi (Info_ValueForKey (player->userinfo, "bottomcolor"));

	while (!(player->team = Hash_Find (player->userinfo->tab, "team")))
			Info_SetValueForKey (player->userinfo, "team", "", 1);
	while (!(player->skinname = Hash_Find (player->userinfo->tab, "skin")))
			Info_SetValueForKey (player->userinfo, "skin", "", 1);

	if (Info_ValueForKey (player->userinfo, "*spectator")[0])
		player->spectator = true;
	else
		player->spectator = false;

	if (cls.state == ca_active)
		Skin_Find (player);

	Sbar_Changed ();
	//XXX CL_NewTranslation (slot);
}

void
CL_ParseUpdateUserInfo (void)
{
	player_info_t *player;
	net_svc_updateuserinfo_t updateuserinfo;

	if (NET_SVC_UpdateUserInfo_Parse (&updateuserinfo, net_message)) {
		Host_NetError ("CL_ParseUpdateUserInfo: Bad Read\n");
		return;
	}

	if (updateuserinfo.slot >= MAX_CLIENTS) {
		Host_NetError ("CL_ParseUpdateUserInfo: slot %i >= MAX_CLIENTS",
					   updateuserinfo.slot);
		return;
	}

	player = &cl.players[updateuserinfo.slot];
	player->userid = updateuserinfo.userid;
	if (player->userinfo)
		Info_Destroy (player->userinfo);
	player->userinfo = Info_ParseString (updateuserinfo.userinfo,
										 MAX_INFO_STRING);

	CL_ProcessUserInfo (updateuserinfo.slot, player);
}

void
CL_SetInfo (void)
{
	int				flags;
	player_info_t  *player;
	net_svc_setinfo_t setinfo;

	if (NET_SVC_SetInfo_Parse (&setinfo, net_message)) {
		Host_NetError ("CL_SetInfo: Bad Read\n");
		return;
	}

	if (setinfo.slot >= MAX_CLIENTS) {
		Host_NetError ("CL_SetInfo: slot %i >= MAX_CLIENTS", setinfo.slot);
		return;
	}

	player = &cl.players[setinfo.slot];
	if (!player->userinfo)
		player->userinfo = Info_ParseString ("", MAX_INFO_STRING);

	Con_DPrintf ("SETINFO %s: %s=%s\n", player->name, setinfo.key,
				 setinfo.value);

	flags = !strequal (setinfo.key, "name");
	flags |= strequal (setinfo.key, "team") << 1;
	Info_SetValueForKey (player->userinfo, setinfo.key, setinfo.value, flags);

	CL_ProcessUserInfo (setinfo.slot, player);
}

void
CL_ServerInfo (void)
{
	net_svc_serverinfo_t block;

	if (NET_SVC_ServerInfo_Parse (&block, net_message)) {
		Host_NetError ("CL_ServerInfo: Bad Read\n");
		return;
	}

	Con_DPrintf ("SERVERINFO: %s=%s\n", block.key, block.value);

	Info_SetValueForKey (cl.serverinfo, block.key, block.value, 0);
	if (strequal (block.key, "chase")) {
		cl.chase = atoi (block.value);
	} else if (strequal (block.key, "watervis")) {
		cl.watervis = atoi (block.value);
	}
}

void
CL_SetStat (int stat, int value)
{
	int         j;

	if (stat < 0 || stat >= MAX_CL_STATS)
//		Sys_Error ("CL_SetStat: %i is invalid", stat);
		Host_NetError ("CL_SetStat: %i is invalid", stat);

	Sbar_Changed ();

	switch (stat) {
		case STAT_ITEMS:				// set flash times
			Sbar_Changed ();
			for (j = 0; j < 32; j++)
				if ((value & (1 << j)) && !(cl.stats[stat] & (1 << j)))
					cl.item_gettime[j] = cl.time;
			break;
		case STAT_HEALTH:
			if (value <= 0)
				Team_Dead ();
			break;
	}

	cl.stats[stat] = value;
}

void
CL_MuzzleFlash (void)
{
	dlight_t   *dl;
	int         i;
	player_state_t *pl;
	vec3_t      fv, rv, uv;

	i = MSG_ReadShort (net_message);

	if ((unsigned int) (i - 1) >= MAX_CLIENTS)
		return;

	pl = &cl.frames[parsecountmod].playerstate[i - 1];

	dl = R_AllocDlight (i); //FIXME
	// this interfers with powerup glows, but we need more lights.
	VectorCopy (pl->origin, dl->origin);
	if (i - 1 == cl.playernum)
		AngleVectors (cl.viewangles, fv, rv, uv);
	else
		AngleVectors (pl->viewangles, fv, rv, uv);

	VectorMA (dl->origin, 18, fv, dl->origin);
	dl->radius = 200 + (rand () & 31);
	dl->minlight = 32;
	dl->die = cl.time + 0.1;
	dl->color[0] = 0.2;
	dl->color[1] = 0.1;
	dl->color[2] = 0.05;
}

#define SHOWNET(x) if (cl_shownet->int_val == 2) Con_Printf ("%3i:%s\n", net_message->readcount-1, x);

int         received_framecount;

void
CL_ParseServerMessage (void)
{
	const char *s;
	int         cmd, i, j;

	received_framecount = host_framecount;
	cl.last_servermessage = realtime;
	CL_ClearProjectiles ();

	// if recording demos, copy the message out
	if (cl_shownet->int_val == 1)
		Con_Printf ("%i ", net_message->message->cursize);
	else if (cl_shownet->int_val == 2)
		Con_Printf ("------------------\n");

	CL_ParseClientdata ();

	// parse the message
	while (1) {
		if (cls.state == ca_disconnected)
			break; // something called Host_NetError

		if (net_message->badread) {
			Host_NetError ("CL_ParseServerMessage: Bad server message");
			break;
		}

		cmd = MSG_ReadByte (net_message);

		if (cmd == -1) {
			net_message->readcount++;	// so the EOM showner has the right
										// value
			SHOWNET ("END OF MESSAGE");
			break;
		}

		SHOWNET (svc_strings[cmd]);

		// other commands
		switch (cmd) {
			default:
				Host_NetError ("CL_ParseServerMessage: Illegible "
							   "server message");
				break;

			case svc_nop:
//				Con_Printf ("svc_nop\n");
				break;

			case svc_disconnect:
				if (cls.state == ca_connected)
					Host_EndGame ("Server disconnected\n"
								  "Server version may not be compatible");
				else
					Host_EndGame ("Server disconnected");
				break;

			case svc_print:
				CL_ParsePrint ();
				break;

			case svc_centerprint:
				SCR_CenterPrint (MSG_ReadString (net_message));
				break;

			case svc_stufftext:
				s = MSG_ReadString (net_message);
				Con_DPrintf ("stufftext: %s\n", s);
				Cbuf_AddText (s);
				break;

			case svc_damage:
				V_ParseDamage ();
				break;

			case svc_serverdata:
				Cbuf_Execute ();		// make sure any stuffed commands are 
										// done
				CL_ParseServerData ();
				vid.recalc_refdef = true;	// leave full screen intermission
				break;

			case svc_setangle:
				for (i = 0; i < 3; i++)
					cl.viewangles[i] = MSG_ReadAngle (net_message);
//				cl.viewangles[PITCH] = cl.viewangles[ROLL] = 0;
				break;

			case svc_lightstyle:
				i = MSG_ReadByte (net_message);
				if (i >= MAX_LIGHTSTYLES)
//					Sys_Error ("svc_lightstyle > MAX_LIGHTSTYLES");
					Host_NetError ("svc_lightstyle > MAX_LIGHTSTYLES");
				strncpy (r_lightstyle[i].map, MSG_ReadString (net_message),
						 sizeof (r_lightstyle[i].map) - 1);
				r_lightstyle[i].map[sizeof (r_lightstyle[i].map) - 1] = 0;
				r_lightstyle[i].length = strlen (r_lightstyle[i].map);
				break;

			case svc_sound:
				CL_ParseStartSoundPacket ();
				break;

			case svc_stopsound:
				i = MSG_ReadShort (net_message);
				S_StopSound (i >> 3, i & 7);
				break;

			case svc_updatefrags:
				Sbar_Changed ();
				i = MSG_ReadByte (net_message);
				if (i >= MAX_CLIENTS)
					Host_NetError ("CL_ParseServerMessage: svc_updatefrags > "
								   "MAX_SCOREBOARD");
				cl.players[i].frags = MSG_ReadShort (net_message);
				break;

			case svc_updateping:
				i = MSG_ReadByte (net_message);
				if (i >= MAX_CLIENTS)
					Host_NetError ("CL_ParseServerMessage: svc_updateping > "
								   "MAX_SCOREBOARD");
				cl.players[i].ping = MSG_ReadShort (net_message);
				break;

			case svc_updatepl:
				i = MSG_ReadByte (net_message);
				if (i >= MAX_CLIENTS)
					Host_NetError ("CL_ParseServerMessage: svc_updatepl > "
								   "MAX_SCOREBOARD");
				cl.players[i].pl = MSG_ReadByte (net_message);
				break;

			case svc_updateentertime:
				// time is sent over as seconds ago
				i = MSG_ReadByte (net_message);
				if (i >= MAX_CLIENTS)
					Host_NetError ("CL_ParseServerMessage: svc_updateentertime "
								   "> MAX_SCOREBOARD");
				cl.players[i].entertime = realtime - MSG_ReadFloat
					(net_message);
				break;

			case svc_spawnbaseline:
				CL_ParseSpawnBaseline ();
				break;
			case svc_spawnstatic:
				CL_ParseStatic ();
				break;
			case svc_temp_entity:
				CL_ParseTEnt ();
				break;

			case svc_killedmonster:
				cl.stats[STAT_MONSTERS]++;
				break;

			case svc_foundsecret:
				cl.stats[STAT_SECRETS]++;
				break;

			case svc_updatestat:
				i = MSG_ReadByte (net_message);
				j = MSG_ReadByte (net_message);
				CL_SetStat (i, j);
				break;
			case svc_updatestatlong:
				i = MSG_ReadByte (net_message);
				j = MSG_ReadLong (net_message);
				CL_SetStat (i, j);
				break;

			case svc_spawnstaticsound:
				CL_ParseStaticSound ();
				break;

			case svc_cdtrack:
				cl.cdtrack = MSG_ReadByte (net_message);
				CDAudio_Play ((byte) cl.cdtrack, true);
				break;

			case svc_intermission:
				Con_DPrintf ("svc_intermission\n");
				cl.intermission = 1;
				cl.completed_time = realtime;
				vid.recalc_refdef = true;	// go to full screen
				Con_DPrintf ("intermission simorg: ");
				for (i = 0; i < 3; i++) {
					cl.simorg[i] = MSG_ReadCoord (net_message);
					Con_DPrintf ("%f ", cl.simorg[i]);
				}
				Con_DPrintf ("\nintermission simangles: ");
				for (i = 0; i < 3; i++) {
					cl.simangles[i] = MSG_ReadAngle (net_message);
					Con_DPrintf ("%f ", cl.simangles[i]);
				}
				Con_DPrintf ("\n");
				VectorCopy (vec3_origin, cl.simvel);
				break;

			case svc_finale:
				cl.intermission = 2;
				cl.completed_time = realtime;
				vid.recalc_refdef = true;	// go to full screen
				SCR_CenterPrint (MSG_ReadString (net_message));
				break;

			case svc_sellscreen:
				Cmd_ExecuteString ("help", src_command);
				break;

			case svc_smallkick:
				cl.punchangle = -2;
				break;
			case svc_bigkick:
				cl.punchangle = -4;
				break;

			case svc_muzzleflash:
				CL_MuzzleFlash ();
				break;

			case svc_updateuserinfo:
				CL_ParseUpdateUserInfo ();
				break;

			case svc_setinfo:
				CL_SetInfo ();
				break;

			case svc_serverinfo:
				CL_ServerInfo ();
				break;

			case svc_download:
				CL_ParseDownload ();
				break;

			case svc_playerinfo:
				CL_ParsePlayerinfo ();
				break;

			case svc_nails:
				CL_ParseProjectiles ();
				break;

			case svc_chokecount:		// some preceding packets were choked
				i = MSG_ReadByte (net_message);
				for (j = 0; j < i; j++)
					cl.
						frames[(cls.netchan.incoming_acknowledged - 1 - j) &
							   UPDATE_MASK].receivedtime = -2;
				break;

			case svc_modellist:
				CL_ParseModellist ();
				break;

			case svc_soundlist:
				CL_ParseSoundlist ();
				break;

			case svc_packetentities:
				CL_ParsePacketEntities ();
				break;

			case svc_deltapacketentities:
				CL_ParseDeltaPacketEntities ();
				break;

			case svc_maxspeed:
				movevars.maxspeed = MSG_ReadFloat (net_message);
				break;

			case svc_entgravity:
				movevars.entgravity = MSG_ReadFloat (net_message);
				break;

			case svc_setpause:
				cl.paused = MSG_ReadByte (net_message);
				if (cl.paused)
					CDAudio_Pause ();
				else
					CDAudio_Resume ();
				break;

		}
	}

	CL_SetSolidEntities ();
}
