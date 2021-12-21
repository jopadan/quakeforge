#include "input.h"

in_button_t *IN_CreateButton (string name, string description) = #0;
in_axis_t *IN_CreateAxis (string name, string description) = #0;
int IN_FindDeviceId (string _id) = #0;
string IN_GetDeviceName (int devid) = #0;
string IN_GetDeviceId (int devid) = #0;
//IN_AxisInfo () = #0;
//IN_ButtonInfo () = #0;
string IN_GetAxisName (int devid, int axis) = #0;
string IN_GetButtonName (int devid, int axis) = #0;
int IN_GetAxisNumber (int devid, string axis) = #0;
int IN_GetButtonNumber (int devid, string axis) = #0;
void IN_ProcessEvents (void) = #0;
void IN_ClearStates (void) = #0;
int IN_GetAxisInfo (int devid, int axis, in_axisinfo_t *info) = #0;
int IN_GetButtonInfo (int devid, int button, in_buttoninfo_t *info) = #0;
@overload
void IN_ButtonAddListener (in_button_t *button, button_listener_t listener,
						   void *data) = #0;
@overload
void IN_ButtonRemoveListener (in_button_t *button, button_listener_t listener,
							  void *data) = #0;
@overload
void IN_AxisAddListener (in_axis_t *axis, axis_listener_t listener,
						 void *data) = #0;
@overload
void IN_AxisRemoveListener (in_axis_t *axis, axis_listener_t listener,
							void *data) = #0;
@overload
void IN_ButtonAddListener (in_button_t *button, IMP listener, id obj) = #0;
@overload
void IN_ButtonRemoveListener (in_button_t *button, IMP listener, id obj) = #0;
@overload
void IN_AxisAddListener (in_axis_t *axis, IMP listener, id obj) = #0;
@overload
void IN_AxisRemoveListener (in_axis_t *axis, IMP listener, id obj) = #0;