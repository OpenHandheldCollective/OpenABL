/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Settings - Loader settings and the rules for changing them.
*/

#include "Core/Settings.h"

#include "Base/Buffer.h"
#include "Base/Text.h"

CONST UINT8 gSettingsTimeoutChoices[4] = {0, 3, 5, 10};

VOID
SettingsDefault (OUT SETTINGS *Settings)
{
  *Settings = (SETTINGS){
    .BootMode      = BOOT_MODE_LINUX,
    .AblMode       = ABL_MODE_SILENT,
    .BootSource    = BOOT_SOURCE_AUTO,
    .Rotation      = ROTATION_AUTO,
    .ModelRotation = ROTATION_NONE,
  };
}

VOID
SettingsToggleBootMode (
  IN OUT SETTINGS        *Settings,
  IN CONST SETTINGS_CAPS *Caps
)
{
  if (Settings->BootMode == BOOT_MODE_LINUX) {
    Settings->BootMode   = BOOT_MODE_ANDROID;
    Settings->BootSource = BOOT_SOURCE_INTERNAL;
  } else {
    Settings->BootMode = BOOT_MODE_LINUX;
    if (Settings->BootSource == BOOT_SOURCE_INTERNAL && !Caps->CfwOnInternal) {
      Settings->BootSource = BOOT_SOURCE_SD;
    }
  }
}

VOID
SettingsNextBootSource (
  IN OUT SETTINGS        *Settings,
  IN CONST SETTINGS_CAPS *Caps
)
{
  switch (Settings->BootSource) {
  case BOOT_SOURCE_AUTO:
    Settings->BootSource =
      Caps->CfwOnInternal ? BOOT_SOURCE_INTERNAL : BOOT_SOURCE_SD;
    break;
  case BOOT_SOURCE_INTERNAL:
    Settings->BootSource = BOOT_SOURCE_SD;
    break;
  case BOOT_SOURCE_SD:
    Settings->BootSource = Caps->UsbHost ? BOOT_SOURCE_USB : BOOT_SOURCE_AUTO;
    break;
  case BOOT_SOURCE_USB:
    Settings->BootSource = BOOT_SOURCE_AUTO;
    break;
  }
}

BOOLEAN
SettingsBootSourceVisible (
  IN CONST SETTINGS      *Settings,
  IN CONST SETTINGS_CAPS *Caps
)
{
  if (Settings->BootMode == BOOT_MODE_ANDROID) {
    return FALSE;
  }
  return Caps->UsbHost || Caps->CfwOnInternal ||
         Settings->BootSource != BOOT_SOURCE_SD;
}

BOOLEAN
SettingsModelVisible (IN CONST SETTINGS *Settings)
{
  return Settings->BootMode != BOOT_MODE_ANDROID;
}

VOID
SettingsNextRotation (IN OUT SETTINGS *Settings)
{
  switch (Settings->Rotation) {
  case ROTATION_AUTO:
    Settings->Rotation = 0;
    break;
  case 0:
    Settings->Rotation = 90;
    break;
  case 90:
    Settings->Rotation = 180;
    break;
  case 180:
    Settings->Rotation = 270;
    break;
  default:
    Settings->Rotation = ROTATION_AUTO;
    break;
  }
}

UINT16
SettingsEffectiveRotation (IN CONST SETTINGS *Settings)
{
  if (Settings->Rotation == ROTATION_AUTO) {
    return Settings->ModelRotation == ROTATION_NONE
             ? 0
             : Settings->ModelRotation % 360;
  }
  return Settings->Rotation % 360;
}

VOID
SettingsNextTimeout (IN OUT SETTINGS *Settings)
{
  UINTN Count = sizeof (gSettingsTimeoutChoices);
  UINTN Index = 0;

  while (Index < Count &&
         gSettingsTimeoutChoices[Index] != Settings->MenuTimeoutS) {
    Index++;
  }
  if (Index == Count) {
    Index = 0;
  }
  Settings->MenuTimeoutS = gSettingsTimeoutChoices[(Index + 1) % Count];
}

CONST CHAR8 *
SettingsAltVolumeLabel (IN CONST SETTINGS *Settings)
{
  if (Settings->AltVolumeUp) {
    return "Linux (external)";
  }
  if (Settings->BootMode == BOOT_MODE_ANDROID) {
    return "Recovery";
  }
  return "Android";
}

// Variables

STATIC VOID
Put (
  OUT SETTINGS_VAR *Var,
  IN CONST CHAR8   *Name,
  IN CONST VOID    *Data,
  IN UINTN          Size
)
{
  Var->Name = Name;
  Var->Len  = Size;
  for (UINTN Index = 0; Index < Size; Index++) {
    Var->Value[Index] = ((CONST UINT8 *)Data)[Index];
  }
}

STATIC VOID
PutU8 (
  OUT SETTINGS_VAR *Var,
  IN CONST CHAR8   *Name,
  IN UINT8          Value
)
{
  Put (Var, Name, &Value, 1);
}

STATIC VOID
PutLe16 (
  OUT SETTINGS_VAR *Var,
  IN CONST CHAR8   *Name,
  IN UINT16         Value
)
{
  UINT8 Bytes[2];

  WriteLe16 (Bytes, Value);
  Put (Var, Name, Bytes, 2);
}

UINTN
SettingsToVars (
  IN CONST SETTINGS *Settings,
  OUT SETTINGS_VAR   Out[SETTINGS_VARS]
)
{
  UINTN Count    = 0;
  UINTN ModelLen = 0;

  while (ModelLen < SETTINGS_MAX_MODEL_LEN && Settings->Model[ModelLen]) {
    ModelLen++;
  }
  // Codes are the enum values. ROTATION_AUTO and ROTATION_NONE are stored
  // as 0xFFFF.
  PutU8 (&Out[Count++], SETTINGS_KEY_SCHEMA, SETTINGS_SCHEMA);
  PutU8 (&Out[Count++], SETTINGS_KEY_BOOT_MODE, (UINT8)Settings->BootMode);
  PutU8 (&Out[Count++], SETTINGS_KEY_ABL_MODE, (UINT8)Settings->AblMode);
  PutU8 (&Out[Count++], SETTINGS_KEY_ALT_VOLUME_UP, Settings->AltVolumeUp);
  PutU8 (&Out[Count++], SETTINGS_KEY_BOOT_SOURCE, (UINT8)Settings->BootSource);
  PutU8 (
    &Out[Count++], SETTINGS_KEY_OFFMODE_CHARGING, Settings->OffmodeCharging
  );
  PutLe16 (&Out[Count++], SETTINGS_KEY_ROTATION, Settings->Rotation);
  PutU8 (&Out[Count++], SETTINGS_KEY_MENU_TIMEOUT, Settings->MenuTimeoutS);
  Put (&Out[Count++], SETTINGS_KEY_MODEL, Settings->Model, ModelLen);
  PutLe16 (&Out[Count++], SETTINGS_KEY_MODEL_ROTATION, Settings->ModelRotation);
  PutU8 (&Out[Count++], SETTINGS_KEY_CUSTOM_LOGO, Settings->CustomLogo);
  PutU8 (&Out[Count++], SETTINGS_KEY_KEEP_LOGO, Settings->KeepLogo);
  return Count;
}

STATIC BOOLEAN
Same (
  IN CONST SETTINGS_VAR *A,
  IN CONST SETTINGS_VAR *B
)
{
  if (A->Len != B->Len) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < A->Len; Index++) {
    if (A->Value[Index] != B->Value[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

UINTN
SettingsChangedVars (
  IN CONST SETTINGS *Settings,
  IN CONST SETTINGS *Next,
  OUT SETTINGS_VAR   Out[SETTINGS_VARS]
)
{
  SETTINGS_VAR Old[SETTINGS_VARS];
  UINTN        Count   = SettingsToVars (Settings, Old);
  UINTN        Changed = 0;

  SettingsToVars (Next, Out);
  for (UINTN Index = 0; Index < Count; Index++) {
    if (!Same (&Old[Index], &Out[Index])) {
      Out[Changed++] = Out[Index];
    }
  }
  return Changed;
}

// Reads a variable that must be Size bytes long.
STATIC BOOLEAN
GetExact (
  IN SETTINGS_GET *Get,
  IN VOID         *Context OPTIONAL,
  IN CONST CHAR8  *Name,
  IN UINTN         Size,
  OUT UINT8       *Out
)
{
  OABL_BUFFER Buffer = {0};
  BOOLEAN     Ok     = Get (Context, Name, &Buffer) && Buffer.Len == Size;

  for (UINTN Index = 0; Ok && Index < Size; Index++) {
    Out[Index] = Buffer.Data[Index];
  }
  BufferFree (&Buffer);
  return Ok;
}

STATIC BOOLEAN
Byte (
  IN SETTINGS_GET *Get,
  IN VOID         *Context OPTIONAL,
  IN CONST CHAR8  *Name,
  OUT UINT8       *Value
)
{
  return GetExact (Get, Context, Name, 1, Value);
}

STATIC BOOLEAN
Word (
  IN SETTINGS_GET *Get,
  IN VOID         *Context OPTIONAL,
  IN CONST CHAR8  *Name,
  OUT UINT16      *Value
)
{
  UINT8 Bytes[2];

  if (!GetExact (Get, Context, Name, 2, Bytes)) {
    return FALSE;
  }
  *Value = ReadLe16 (Bytes);
  return TRUE;
}

BOOLEAN
SettingsInVars (
  IN SETTINGS_GET *Get,
  IN VOID         *Context OPTIONAL
)
{
  UINT8 Schema;

  return Byte (Get, Context, SETTINGS_KEY_SCHEMA, &Schema) &&
         Schema == SETTINGS_SCHEMA;
}

STATIC BOOLEAN
TimeoutChoice (IN UINT8 Timeout)
{
  for (UINTN Index = 0; Index < sizeof (gSettingsTimeoutChoices); Index++) {
    if (gSettingsTimeoutChoices[Index] == Timeout) {
      return TRUE;
    }
  }
  return FALSE;
}

VOID
SettingsFromVars (
  IN CONST SETTINGS *Defaults,
  IN SETTINGS_GET   *Get,
  IN VOID           *Context OPTIONAL,
  OUT SETTINGS      *Out
)
{
  SETTINGS    Settings = *Defaults;
  OABL_BUFFER Model    = {0};
  UINT8       Byte8;
  UINT16      Word16;

  if (!Byte (Get, Context, SETTINGS_KEY_SCHEMA, &Byte8) ||
      Byte8 != SETTINGS_SCHEMA) {
    *Out = Settings;
    return;
  }
  if (Byte (Get, Context, SETTINGS_KEY_BOOT_MODE, &Byte8) && Byte8 <= 1) {
    Settings.BootMode = Byte8 ? BOOT_MODE_ANDROID : BOOT_MODE_LINUX;
  }
  if (Byte (Get, Context, SETTINGS_KEY_ABL_MODE, &Byte8) && Byte8 <= 1) {
    Settings.AblMode = Byte8 ? ABL_MODE_VERBOSE : ABL_MODE_SILENT;
  }
  if (Byte (Get, Context, SETTINGS_KEY_ALT_VOLUME_UP, &Byte8) && Byte8 <= 1) {
    Settings.AltVolumeUp = Byte8 == 1;
  }
  // Stored as the enum value: Auto 0, Internal 1, SD card 2, USB 3.
  if (Byte (Get, Context, SETTINGS_KEY_BOOT_SOURCE, &Byte8) &&
      Byte8 <= BOOT_SOURCE_USB) {
    Settings.BootSource = (BOOT_SOURCE)Byte8;
  }
  if (Byte (Get, Context, SETTINGS_KEY_OFFMODE_CHARGING, &Byte8) &&
      Byte8 <= 1) {
    Settings.OffmodeCharging = Byte8 == 1;
  }
  if (Word (Get, Context, SETTINGS_KEY_ROTATION, &Word16) &&
      (Word16 == ROTATION_AUTO || Word16 == 0 || Word16 == 90 ||
       Word16 == 180 || Word16 == 270)) {
    Settings.Rotation = Word16;
  }
  if (Byte (Get, Context, SETTINGS_KEY_MENU_TIMEOUT, &Byte8) &&
      TimeoutChoice (Byte8)) {
    Settings.MenuTimeoutS = Byte8;
  }
  // A model that is too long is ignored. Invalid UTF-8 clears the model.
  if (Get (Context, SETTINGS_KEY_MODEL, &Model) &&
      Model.Len <= SETTINGS_MAX_MODEL_LEN) {
    UINTN Len = Utf8Valid (Model.Data, Model.Len) ? Model.Len : 0;

    for (UINTN Index = 0; Index < Len; Index++) {
      Settings.Model[Index] = (CHAR8)Model.Data[Index];
    }
    Settings.Model[Len] = 0;
  }
  BufferFree (&Model);
  if (Word (Get, Context, SETTINGS_KEY_MODEL_ROTATION, &Word16)) {
    Settings.ModelRotation = Word16 == ROTATION_NONE || Word16 % 90 != 0
                               ? ROTATION_NONE
                               : Word16 % 360;
  }
  if (Byte (Get, Context, SETTINGS_KEY_CUSTOM_LOGO, &Byte8) && Byte8 <= 1) {
    Settings.CustomLogo = Byte8 == 1;
  }
  if (Byte (Get, Context, SETTINGS_KEY_KEEP_LOGO, &Byte8) && Byte8 <= 1) {
    Settings.KeepLogo = Byte8 == 1;
  }
  *Out = Settings;
}
