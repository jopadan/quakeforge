/*
	cd_win.c

	support for cd in windows

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

#include <windows.h>

#include "QF/cdaudio.h"
#include "QF/cmd.h"
#include "QF/cvar.h"
#include "QF/plugin.h"
#include "QF/qargs.h"
#include "QF/sound.h"
#include "QF/sys.h"

#include "compat.h"

static plugin_t		plugin_info; 
static plugin_data_t	plugin_info_data; 
static plugin_funcs_t	plugin_info_funcs; 
static general_data_t	plugin_info_general_data; 
static general_funcs_t	plugin_info_general_funcs; 
//static cd_data_t		plugin_info_cd_data; 
static cd_funcs_t		plugin_info_cd_funcs;

extern HWND mainwindow;

static qboolean cdValid = false;
static qboolean playing = false;
static qboolean wasPlaying = false;
static qboolean initialized = false;
static qboolean enabled = false;
static qboolean playLooping = false;
static float cdvolume;
static byte remap[100];
static byte playTrack;
static byte maxTrack;

static UINT        wDeviceID;

static void I_CDAudio_Play (int track, qboolean looping);
static void I_CDAudio_Stop (void);


static void
I_CDAudio_CloseDoor (void)
{
	DWORD       dwReturn;

	dwReturn =
		mciSendCommand (wDeviceID, MCI_SET, MCI_SET_DOOR_CLOSED, (DWORD) NULL);
	if (dwReturn) {
		Sys_DPrintf ("MCI_SET_DOOR_CLOSED failed (%li)\n", dwReturn);
	}
}

static void
I_CDAudio_Eject (void)
{
	DWORD       dwReturn;

	dwReturn = mciSendCommand (wDeviceID, MCI_SET, MCI_SET_DOOR_OPEN,
							   (DWORD) NULL);
	if (dwReturn) {
		Sys_DPrintf ("MCI_SET_DOOR_OPEN failed (%li)\n", dwReturn);
	}
}

static int
I_CDAudio_GetAudioDiskInfo (void)
{
	DWORD       dwReturn;
	MCI_STATUS_PARMS mciStatusParms;

	cdValid = false;

	mciStatusParms.dwItem = MCI_STATUS_READY;
	dwReturn =
		mciSendCommand (wDeviceID, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT,
						(DWORD) (LPVOID) & mciStatusParms);
	if (dwReturn) {
		Sys_DPrintf ("CDAudio: drive ready test - get status failed\n");
		return -1;
	}
	if (!mciStatusParms.dwReturn) {
		Sys_DPrintf ("CDAudio: drive not ready\n");
		return -1;
	}

	mciStatusParms.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
	dwReturn =
		mciSendCommand (wDeviceID, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT,
						(DWORD) (LPVOID) & mciStatusParms);
	if (dwReturn) {
		Sys_DPrintf ("CDAudio: get tracks - status failed\n");
		return -1;
	}
	if (mciStatusParms.dwReturn < 1) {
		Sys_DPrintf ("CDAudio: no music tracks\n");
		return -1;
	}

	cdValid = true;
	maxTrack = mciStatusParms.dwReturn;

	return 0;
}

#if 0
LONG
static I_CDAudio_MessageHandler (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (lParam != wDeviceID)
		return 1;

	switch (wParam) {
		case MCI_NOTIFY_SUCCESSFUL:
			if (playing) {
				playing = false;
				if (playLooping)
					I_CDAudio_Play (playTrack, true);
			}
			break;

		case MCI_NOTIFY_ABORTED:
		case MCI_NOTIFY_SUPERSEDED:
			break;

		case MCI_NOTIFY_FAILURE:
			Sys_DPrintf ("MCI_NOTIFY_FAILURE\n");
			I_CDAudio_Stop ();
			cdValid = false;
			break;

		default:
			Sys_DPrintf ("Unexpected MM_MCINOTIFY type (%i)\n", wParam);
			return 1;
	}

	return 0;
}
#endif

static void
I_CDAudio_Pause (void)
{
	DWORD       dwReturn;
	MCI_GENERIC_PARMS mciGenericParms;

	if (!enabled)
		return;

	if (!playing)
		return;

	mciGenericParms.dwCallback = (DWORD) mainwindow;
	dwReturn =
		mciSendCommand (wDeviceID, MCI_PAUSE, 0,
						(DWORD) (LPVOID) & mciGenericParms);
	if (dwReturn) {
		Sys_DPrintf ("MCI_PAUSE failed (%li)", dwReturn);
	}

	wasPlaying = playing;
	playing = false;
}

static void
I_CDAudio_Play (int track, qboolean looping)
{
	DWORD       dwReturn;
	MCI_PLAY_PARMS mciPlayParms;
	MCI_STATUS_PARMS mciStatusParms;

	if (!enabled)
		return;

	if (!cdValid) {
		I_CDAudio_GetAudioDiskInfo ();
		if (!cdValid)
			return;
	}

	track = remap[track];

	if (track < 1 || track > maxTrack) {
		I_CDAudio_Stop ();
		return;
	}
	// don't try to play a non-audio track
	mciStatusParms.dwItem = MCI_CDA_STATUS_TYPE_TRACK;
	mciStatusParms.dwTrack = track;
	dwReturn =
		mciSendCommand (wDeviceID, MCI_STATUS,
						MCI_STATUS_ITEM | MCI_TRACK | MCI_WAIT,
						(DWORD) (LPVOID) & mciStatusParms);
	if (dwReturn) {
		Sys_DPrintf ("MCI_STATUS failed (%li)\n", dwReturn);
		return;
	}
	if (mciStatusParms.dwReturn != MCI_CDA_TRACK_AUDIO) {
		Sys_Printf ("CDAudio: track %i is not audio\n", track);
		return;
	}
	// get the length of the track to be played
	mciStatusParms.dwItem = MCI_STATUS_LENGTH;
	mciStatusParms.dwTrack = track;
	dwReturn =
		mciSendCommand (wDeviceID, MCI_STATUS,
						MCI_STATUS_ITEM | MCI_TRACK | MCI_WAIT,
						(DWORD) (LPVOID) & mciStatusParms);
	if (dwReturn) {
		Sys_DPrintf ("MCI_STATUS failed (%li)\n", dwReturn);
		return;
	}

	if (playing) {
		if (playTrack == track)
			return;
		I_CDAudio_Stop ();
	}

	mciPlayParms.dwFrom = MCI_MAKE_TMSF (track, 0, 0, 0);
	mciPlayParms.dwTo = (mciStatusParms.dwReturn << 8) | track;
	mciPlayParms.dwCallback = (DWORD) mainwindow;
	dwReturn =
		mciSendCommand (wDeviceID, MCI_PLAY, MCI_NOTIFY | MCI_FROM | MCI_TO,
						(DWORD) (LPVOID) & mciPlayParms);
	if (dwReturn) {
		Sys_DPrintf ("CDAudio: MCI_PLAY failed (%li)\n", dwReturn);
		return;
	}

	playLooping = looping;
	playTrack = track;
	playing = true;

	if (cdvolume == 0.0)
		I_CDAudio_Pause ();
}

static void
I_CDAudio_Resume (void)
{
	DWORD       dwReturn;
	MCI_PLAY_PARMS mciPlayParms;

	if (!enabled)
		return;

	if (!cdValid)
		return;

	if (!wasPlaying)
		return;

	mciPlayParms.dwFrom = MCI_MAKE_TMSF (playTrack, 0, 0, 0);
	mciPlayParms.dwTo = MCI_MAKE_TMSF (playTrack + 1, 0, 0, 0);
	mciPlayParms.dwCallback = (DWORD) mainwindow;
	dwReturn =
		mciSendCommand (wDeviceID, MCI_PLAY, MCI_TO | MCI_NOTIFY,
						(DWORD) (LPVOID) & mciPlayParms);
	if (dwReturn) {
		Sys_DPrintf ("CDAudio: MCI_PLAY failed (%li)\n", dwReturn);
		return;
	}
	playing = true;
}

static void
I_CDAudio_Shutdown (void)
{
	if (!initialized)
		return;
	I_CDAudio_Stop ();
	if (mciSendCommand (wDeviceID, MCI_CLOSE, MCI_WAIT, (DWORD) NULL))
		Sys_DPrintf ("CDAudio_Shutdown: MCI_CLOSE failed\n");
}

static void
I_CDAudio_Stop (void)
{
	DWORD       dwReturn;

	if (!enabled)
		return;

	if (!playing)
		return;

	dwReturn = mciSendCommand (wDeviceID, MCI_STOP, 0, (DWORD) NULL);
	if (dwReturn) {
		Sys_DPrintf ("MCI_STOP failed (%li)", dwReturn);
	}

	wasPlaying = false;
	playing = false;
}

static void
I_CDAudio_Update (void)
{
	if (!enabled)
		return;

	if (bgmvolume->value != cdvolume) {
		if (cdvolume) {
			Cvar_SetValue (bgmvolume, 0.0);
			cdvolume = bgmvolume->value;
			I_CDAudio_Pause ();
		} else {
			Cvar_SetValue (bgmvolume, 1.0);
			cdvolume = bgmvolume->value;
			I_CDAudio_Resume ();
		}
	}
}

static void
I_CD_f (void)
{
	const char *command;
	int         n, ret;
//  int     startAddress;

	if (Cmd_Argc () < 2)
		return;

	command = Cmd_Argv (1);

	if (strequal (command, "on")) {
		enabled = true;
		return;
	}

	if (strequal (command, "off")) {
		if (playing)
			I_CDAudio_Stop ();
		enabled = false;
		return;
	}

	if (strequal (command, "reset")) {
		enabled = true;
		if (playing)
			I_CDAudio_Stop ();
		for (n = 0; n < 100; n++)
			remap[n] = n;
		I_CDAudio_GetAudioDiskInfo ();
		return;
	}

	if (strequal (command, "remap")) {
		ret = Cmd_Argc () - 2;
		if (ret <= 0) {
			for (n = 1; n < 100; n++)
				if (remap[n] != n)
					Sys_Printf ("  %u -> %u\n", n, remap[n]);
			return;
		}
		for (n = 1; n <= ret; n++)
			remap[n] = atoi (Cmd_Argv (n + 1));
		return;
	}

	if (strequal (command, "close")) {
		I_CDAudio_CloseDoor ();
		return;
	}

	if (!cdValid) {
		I_CDAudio_GetAudioDiskInfo ();
		if (!cdValid) {
			Sys_Printf ("No CD in player.\n");
			return;
		}
	}

	if (strequal (command, "play")) {
		I_CDAudio_Play ((int) atoi (Cmd_Argv (2)), false);
		return;
	}

	if (strequal (command, "loop")) {
		I_CDAudio_Play ((int) atoi (Cmd_Argv (2)), true);
		return;
	}

	if (strequal (command, "stop")) {
		I_CDAudio_Stop ();
		return;
	}

	if (strequal (command, "pause")) {
		I_CDAudio_Pause ();
		return;
	}

	if (strequal (command, "resume")) {
		I_CDAudio_Resume ();
		return;
	}

	if (strequal (command, "eject")) {
		if (playing)
			I_CDAudio_Stop ();
		I_CDAudio_Eject ();
		cdValid = false;
		return;
	}

	if (strequal (command, "info")) {
		Sys_Printf ("%u tracks\n", maxTrack);
		if (playing)
			Sys_Printf ("Currently %s track %u\n",
						playLooping ? "looping" : "playing", playTrack);
		else if (wasPlaying)
			Sys_Printf ("Paused %s track %u\n",
						playLooping ? "looping" : "playing", playTrack);
		Sys_Printf ("Volume is %f\n", cdvolume);
		return;
	}
}

static void
I_CDAudio_Init (void)
{
	DWORD       dwReturn;
	MCI_OPEN_PARMS mciOpenParms;
	MCI_SET_PARMS mciSetParms;
	int         n;

	mciOpenParms.lpstrDeviceType = "cdaudio";
	dwReturn =
		mciSendCommand (0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_SHAREABLE,
						(DWORD) (LPVOID) & mciOpenParms);
	if (dwReturn) {
		Sys_Printf ("CDAudio_Init: MCI_OPEN failed (%li)\n", dwReturn);
		return; // was -1
	}
	wDeviceID = mciOpenParms.wDeviceID;

	// Set the time format to track/minute/second/frame (TMSF).
	mciSetParms.dwTimeFormat = MCI_FORMAT_TMSF;
	dwReturn =
		mciSendCommand (wDeviceID, MCI_SET, MCI_SET_TIME_FORMAT,
						(DWORD) (LPVOID) & mciSetParms);
	if (dwReturn) {
		Sys_Printf ("MCI_SET_TIME_FORMAT failed (%li)\n", dwReturn);
		mciSendCommand (wDeviceID, MCI_CLOSE, 0, (DWORD) NULL);
		return; // was -1
	}

	for (n = 0; n < 100; n++)
		remap[n] = n;
	initialized = true;
	enabled = true;

	if (I_CDAudio_GetAudioDiskInfo ()) {
		Sys_Printf ("CDAudio_Init: No CD in player.\n");
		cdValid = false;
		enabled = false;
	}
}

QFPLUGIN plugin_t *
cd_win_PluginInfo (void)
{
	plugin_info.type = qfp_cd;
	plugin_info.api_version = QFPLUGIN_VERSION;
	plugin_info.plugin_version = "0.1";
	plugin_info.description = "Linux CD Audio output"
		"Copyright (C) 2001  contributors of the QuakeForge project\n"
		"Please see the file \"AUTHORS\" for a list of contributors\n";
	plugin_info.functions = &plugin_info_funcs;

	plugin_info.data = &plugin_info_data;

	plugin_info_data.general = &plugin_info_general_data;
//	plugin_info_data.cd = &plugin_info_cd_data;
	plugin_info_data.input = NULL;
 
	plugin_info_funcs.general = &plugin_info_general_funcs;
	plugin_info_funcs.cd = &plugin_info_cd_funcs;
	plugin_info_funcs.input = NULL;

	plugin_info_general_funcs.p_Init = I_CDAudio_Init;
	plugin_info_general_funcs.p_Shutdown = I_CDAudio_Shutdown;

	plugin_info_cd_funcs.pCDAudio_Pause = I_CDAudio_Pause;
	plugin_info_cd_funcs.pCDAudio_Play = I_CDAudio_Play;
	plugin_info_cd_funcs.pCDAudio_Resume = I_CDAudio_Resume;
	plugin_info_cd_funcs.pCDAudio_Update = I_CDAudio_Update;
	plugin_info_cd_funcs.pCD_f = I_CD_f;

	return &plugin_info;
}
