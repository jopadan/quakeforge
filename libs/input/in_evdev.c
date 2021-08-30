/*
	in_evdev.c

	general evdev input driver

	Copyright (C) 2021 Bill Currie <bill@taniwha.org>
	Please see the file "AUTHORS" for a list of contributors

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

#include "QF/cvar.h"
#include "QF/in_event.h"
#include "QF/input.h"
#include "QF/progs.h"   // for PR_RESMAP
#include "QF/sys.h"

#include "compat.h"
#include "evdev/inputlib.h"

typedef struct {
	in_device_t in_dev;
	int         devid;
} devmap_t;

static int evdev_driver_handle = -1;
static PR_RESMAP (devmap_t) devmap;

static void
in_evdev_keydest_callback (keydest_t key_dest, void *data)
{
}

static void
in_evdev_process_events (void *data)
{
	if (inputlib_check_input ()) {
	}
}

static void
in_evdev_shutdown (void *data)
{
	inputlib_close ();
}

static void
in_evdev_axis_event (axis_t *axis, void *_dm)
{
	devmap_t   *dm = _dm;
	//Sys_Printf ("in_evdev_axis_event: %d %d\n", axis->num, axis->value);

	IE_event_t  event = {
		.type = ie_axis,
		.when = Sys_LongTime (),
		.axis = {
			.devid = dm->devid,
			.axis = axis->num,
			.value = axis->value,
		},
	};
	IE_Send_Event (&event);
}

static void
in_evdev_button_event (button_t *button, void *_dm)
{
	devmap_t   *dm = _dm;
	//Sys_Printf ("in_evdev_button_event: %d %d\n", button->num, button->state);

	IE_event_t  event = {
		.type = ie_button,
		.when = Sys_LongTime (),
		.button = {
			.devid = dm->devid,
			.button = button->num,
			.state = button->state,
		},
	};
	IE_Send_Event (&event);
}

static void
device_add (device_t *dev)
{
	devmap_t   *dm = PR_RESNEW (devmap);
	dm->in_dev.driverid = evdev_driver_handle;
	dm->in_dev.device = dev;
	dm->in_dev.name = dev->name;
	dm->in_dev.path = dev->phys;
	dm->devid = IN_AddDevice (&dm->in_dev);

	dev->data = dm;
	dev->axis_event = in_evdev_axis_event;
	dev->button_event = in_evdev_button_event;

#if 0
	Sys_Printf ("in_evdev: add %s\n", dev->path);
	Sys_Printf ("    %s\n", dev->name);
	Sys_Printf ("    %s\n", dev->phys);
	for (int i = 0; i < dev->num_axes; i++) {
		axis_t     *axis = dev->axes + i;
		Sys_Printf ("axis: %d %d\n", axis->num, axis->value);
	}
	for (int i = 0; i < dev->num_buttons; i++) {
		button_t     *button = dev->buttons + i;
		Sys_Printf ("button: %d %d\n", button->num, button->state);
	}
#endif
}

static void
device_remove (device_t *dev)
{
	//Sys_Printf ("in_evdev: remove %s\n", dev->path);
	for (unsigned i = 0; i < devmap._size; i++) {
		devmap_t   *dm = PR_RESGET (devmap, ~i);
		if (dm->in_dev.device == dev) {
			IN_RemoveDevice (dm->devid);
			memset (dm, 0, sizeof (*dm));
			PR_RESFREE (devmap, dm);
			break;
		}
	}
}

static void
in_evdev_init (void *data)
{
	Key_KeydestCallback (in_evdev_keydest_callback, 0);

	inputlib_init (device_add, device_remove);
}

static void
in_evdev_clear_states (void *data)
{
}

static in_driver_t in_evdev_driver = {
	.init = in_evdev_init,
	.shutdown = in_evdev_shutdown,
	.process_events = in_evdev_process_events,
	.clear_states = in_evdev_clear_states,
};

static void __attribute__((constructor))
in_evdev_register_driver (void)
{
	evdev_driver_handle = IN_RegisterDriver (&in_evdev_driver, 0);
}

int in_evdev_force_link;
