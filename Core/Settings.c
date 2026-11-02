/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Settings - Loader settings, the rules for changing them, and reading and
  saving them on the device.
*/

#include "Core/Settings.h"

#include "Base/Buffer.h"
#include "Base/Text.h"

// Languages

#define SETTINGS_LANGUAGE_KEY(Id, Symbol) #Symbol,

// In LANG_LIST order, so a LANG_ID indexes it.
STATIC CONST CHAR8 *CONST mLanguageKeys[SETTINGS_LANGUAGES] = {
  LANG_LIST (SETTINGS_LANGUAGE_KEY)};

#define SETTINGS_LANGUAGE_FITS(Id, Symbol) \
  _Static_assert (sizeof (#Symbol) - 1 <= SETTINGS_MAX_LANGUAGE_LEN, #Symbol);
LANG_LIST (SETTINGS_LANGUAGE_FITS)

UINT8
SettingsLanguageFromKey (
  IN CONST UINT8 *Key,
  IN UINTN        Len
)
{
  for (UINT8 Lang = 0; Lang < SETTINGS_LANGUAGES; Lang++) {
    CONST CHAR8 *Have  = mLanguageKeys[Lang];
    UINTN        Index = 0;

    while (Index < Len && Have[Index] && Have[Index] == (CHAR8)Key[Index]) {
      Index++;
    }
    if (Index == Len && Have[Len] == 0) {
      return Lang;
    }
  }
  return SETTINGS_LANGUAGES;
}

// English, wherever LANG_LIST puts it.
STATIC UINT8
SettingsLanguageEnglish (VOID)
{
  return SettingsLanguageFromKey ((CONST UINT8 *)"En", 2);
}

CONST CHAR8 *
SettingsLanguageKey (IN UINT8 Lang)
{
  if (Lang >= SETTINGS_LANGUAGES) {
    Lang = SettingsLanguageEnglish ();
  }
  return mLanguageKeys[Lang];
}

VOID
SettingsDefault (OUT SETTINGS *Settings)
{
  *Settings = (SETTINGS){
    .BootMode      = BOOT_MODE_LINUX,
    .AblMode       = ABL_MODE_SILENT,
    .BootSource    = BOOT_SOURCE_AUTO,
    .Rotation      = ROTATION_AUTO,
    .ModelRotation = ROTATION_NONE,
    .Language      = SettingsLanguageEnglish (),
  };
}

VOID
SettingsToggleBootMode (
  IN OUT SETTINGS        *Settings,
  IN CONST SETTINGS_CAPS *Caps
)
{
  if (Settings->BootMode == BOOT_MODE_LINUX) {
    Settings->BootMode = BOOT_MODE_ANDROID;
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

// Reading and saving, on the device

#ifdef OABL_UEFI

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Efi/Abl.h"
#include "Efi/MenuHost.h"
#include "Efi/Variables.h"

#define STATUS_TEXT(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

STATIC LOG_SINK mLog;

// A setting's variable. An empty value deletes it.
typedef struct _SETTINGS_VAR {
  CONST CHAR8 *Name;
  UINT8        Value[SETTINGS_MAX_MODEL_LEN];
  UINTN        Len;
} SETTINGS_VAR;

_Static_assert (
  SETTINGS_MAX_LANGUAGE_LEN <= SETTINGS_MAX_MODEL_LEN, "a language fits Value"
);

// Where ToVars puts each setting's variable. SETTINGS_VARS counts them.
enum {
  VAR_BOOT_MODE,
  VAR_ABL_MODE,
  VAR_ALT_VOLUME_UP,
  VAR_BOOT_SOURCE,
  VAR_ROTATION,
  VAR_MODEL,
  VAR_MODEL_ROTATION,
  VAR_MODEL_ROTATION_FOR,
  VAR_CUSTOM_LOGO,
  VAR_LANGUAGE,
  VAR_USB_HOST_LIVE,
  VAR_DISPLAY_OFF,
  VAR_PANEL_LINES_OFF,
  SETTINGS_VARS
};

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
  CopyMem (Var->Value, Data, Size);
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

// Converts the settings to the variables that store them, the charger
// screen aside, each at its VAR_ index. Codes are the enum values;
// ROTATION_AUTO and ROTATION_NONE are 0xFFFF.
STATIC VOID
ToVars (
  IN CONST SETTINGS *Settings,
  OUT SETTINGS_VAR   Out[SETTINGS_VARS]
)
{
  UINTN        ModelLen = 0;
  CONST CHAR8 *Language = SettingsLanguageKey (Settings->Language);

  while (ModelLen < SETTINGS_MAX_MODEL_LEN && Settings->Model[ModelLen]) {
    ModelLen++;
  }
  PutU8 (
    &Out[VAR_BOOT_MODE], SETTINGS_KEY_BOOT_MODE, (UINT8)Settings->BootMode
  );
  PutU8 (&Out[VAR_ABL_MODE], SETTINGS_KEY_ABL_MODE, (UINT8)Settings->AblMode);
  PutU8 (
    &Out[VAR_ALT_VOLUME_UP], SETTINGS_KEY_ALT_VOLUME_UP, Settings->AltVolumeUp
  );
  PutU8 (
    &Out[VAR_BOOT_SOURCE], SETTINGS_KEY_BOOT_SOURCE,
    (UINT8)Settings->BootSource
  );
  PutLe16 (&Out[VAR_ROTATION], SETTINGS_KEY_ROTATION, Settings->Rotation);
  Put (&Out[VAR_MODEL], SETTINGS_KEY_MODEL, Settings->Model, ModelLen);
  PutLe16 (
    &Out[VAR_MODEL_ROTATION], SETTINGS_KEY_MODEL_ROTATION,
    Settings->ModelRotation
  );
  // Which model ModelRotation was looked up for, once it was.
  Put (
    &Out[VAR_MODEL_ROTATION_FOR], SETTINGS_KEY_MODEL_ROTATION_FOR,
    Settings->Model, Settings->ModelRotationKnown ? ModelLen : 0
  );
  PutU8 (&Out[VAR_CUSTOM_LOGO], SETTINGS_KEY_CUSTOM_LOGO, Settings->CustomLogo);
  Put (
    &Out[VAR_LANGUAGE], SETTINGS_KEY_LANGUAGE, Language, AsciiStrLen (Language)
  );
  PutU8 (
    &Out[VAR_USB_HOST_LIVE], SETTINGS_KEY_USB_HOST_LIVE, Settings->UsbHostLive
  );
  PutU8 (&Out[VAR_DISPLAY_OFF], SETTINGS_KEY_DISPLAY_OFF, Settings->DisplayOff);
  PutU8 (
    &Out[VAR_PANEL_LINES_OFF], SETTINGS_KEY_PANEL_LINES_OFF,
    Settings->PanelLinesOff
  );
}

// Reads a variable that must be Size bytes long, from the copy in memory,
// so it doesn't allocate. Reads misc first if nothing has yet.
STATIC BOOLEAN
GetExact (
  IN CONST CHAR8 *Name,
  IN UINTN        Size,
  OUT UINT8      *Out
)
{
  UINTN Length;

  VariablesLoad (LOG_QUIET);
  return VariablesPeek (Name, Out, Size, &Length) && Length == Size;
}

STATIC BOOLEAN
Byte (
  IN CONST CHAR8 *Name,
  OUT UINT8      *Value
)
{
  return GetExact (Name, 1, Value);
}

STATIC BOOLEAN
Word (
  IN CONST CHAR8 *Name,
  OUT UINT16     *Value
)
{
  UINT8 Bytes[2];

  if (!GetExact (Name, 2, Bytes)) {
    return FALSE;
  }
  *Value = ReadLe16 (Bytes);
  return TRUE;
}

VOID
SettingsSetUp (IN LOG_SINK Log)
{
  mLog = Log;
  VariablesSetUp (Log);
}

// TRUE if the stored panel rotation was looked up for Model.
STATIC BOOLEAN
RotationFor (IN CONST CHAR8 *Model)
{
  UINT8 Owner[SETTINGS_MAX_MODEL_LEN];
  UINTN Length;

  return Model[0] &&
         VariablesPeek (
           SETTINGS_KEY_MODEL_ROTATION_FOR, Owner, sizeof (Owner), &Length
         ) &&
         TextEq ((OABL_TEXT){(CONST CHAR8 *)Owner, Length}, TextOf (Model));
}

VOID
SettingsLoad (OUT SETTINGS *Out)
{
  SETTINGS Settings;
  UINT8    Model[SETTINGS_MAX_MODEL_LEN];
  UINT8    Language[SETTINGS_MAX_LANGUAGE_LEN];
  UINTN    Length;
  UINT8    Byte8;
  UINT16   Word16;

  // Read misc if it isn't read yet. The rest reads the copy in memory
  // without allocating, so this works at ExitBootServices.
  VariablesLoad (LOG_QUIET);
  SettingsDefault (&Settings);
  // A model that is too long is ignored. Invalid UTF-8 clears the model.
  if (VariablesPeek (SETTINGS_KEY_MODEL, Model, sizeof (Model), &Length)) {
    if (!Utf8Valid (Model, Length)) {
      Length = 0;
    }
    CopyMem (Settings.Model, Model, Length);
    Settings.Model[Length] = 0;
  }
  if (Byte (SETTINGS_KEY_BOOT_MODE, &Byte8) && Byte8 <= 1) {
    Settings.BootMode = Byte8 ? BOOT_MODE_ANDROID : BOOT_MODE_LINUX;
  }
  if (Byte (SETTINGS_KEY_ABL_MODE, &Byte8) && Byte8 <= 1) {
    Settings.AblMode = Byte8 ? ABL_MODE_VERBOSE : ABL_MODE_SILENT;
  }
  if (Byte (SETTINGS_KEY_ALT_VOLUME_UP, &Byte8) && Byte8 <= 1) {
    Settings.AltVolumeUp = Byte8 == 1;
  }
  // Stored as the enum value: Auto 0, Internal 1, SD card 2, USB 3.
  if (Byte (SETTINGS_KEY_BOOT_SOURCE, &Byte8) && Byte8 <= BOOT_SOURCE_USB) {
    Settings.BootSource = (BOOT_SOURCE)Byte8;
  }
  if (Word (SETTINGS_KEY_ROTATION, &Word16) &&
      (Word16 == ROTATION_AUTO || Word16 == 0 || Word16 == 90 ||
       Word16 == 180 || Word16 == 270)) {
    Settings.Rotation = Word16;
  }
  // The panel rotation counts only for the model it was looked up for.
  // Missing, it is ROTATION_NONE, the default.
  Settings.ModelRotationKnown = RotationFor (Settings.Model);
  if (Settings.ModelRotationKnown &&
      Word (SETTINGS_KEY_MODEL_ROTATION, &Word16)) {
    Settings.ModelRotation = Word16 == ROTATION_NONE || Word16 % 90 != 0
                               ? ROTATION_NONE
                               : Word16 % 360;
  }
  if (Byte (SETTINGS_KEY_CUSTOM_LOGO, &Byte8) && Byte8 <= 1) {
    Settings.CustomLogo = Byte8 == 1;
  }
  if (VariablesPeek (
        SETTINGS_KEY_LANGUAGE, Language, sizeof (Language), &Length
      )) {
    Byte8 = SettingsLanguageFromKey (Language, Length);
    if (Byte8 < SETTINGS_LANGUAGES) {
      Settings.Language = Byte8;
    }
  }
#ifdef OABL_DEBUG
  if (Byte (SETTINGS_KEY_USB_HOST_LIVE, &Byte8) && Byte8 <= 1) {
    Settings.UsbHostLive = Byte8 == 1;
  }
  if (Byte (SETTINGS_KEY_DISPLAY_OFF, &Byte8) && Byte8 <= 1) {
    Settings.DisplayOff = Byte8 == 1;
  }
  if (Byte (SETTINGS_KEY_PANEL_LINES_OFF, &Byte8) && Byte8 <= 1) {
    Settings.PanelLinesOff = Byte8 == 1;
  }
#endif
  *Out = Settings;
}

STATIC BOOLEAN
SameVar (
  IN CONST SETTINGS_VAR *A,
  IN CONST SETTINGS_VAR *B
)
{
  return A->Len == B->Len &&
         (A->Len == 0 || CompareMem (A->Value, B->Value, A->Len) == 0);
}

// Formats a variable's value for the log.
STATIC VOID
Shown (
  IN CONST SETTINGS_VAR *Var,
  OUT CHAR8             *Out,
  IN UINTN               OutSize
)
{
  if (!Var->Len) {
    AsciiStrCopy (Out, OutSize, "default (deleted)");
  } else if (AsciiStrEq (Var->Name, SETTINGS_KEY_MODEL) ||
             AsciiStrEq (Var->Name, SETTINGS_KEY_MODEL_ROTATION_FOR) ||
             AsciiStrEq (Var->Name, SETTINGS_KEY_LANGUAGE)) {
    CHAR8 *Text = Utf8Lossy (Var->Value, Var->Len);

    SPrint (Out, OutSize, "\"%s\"", Text ? Text : "");
    FreePool (Text);
  } else if (Var->Len == 2) {
    SPrint (Out, OutSize, "%u", ReadLe16 (Var->Value));
  } else {
    SPrint (Out, OutSize, "%u", Var->Value[0]);
  }
}

// Writes misc if a put changed the store. A failed write drops what was
// put, so the settings read back what misc holds.
STATIC EFI_STATUS
Flush (VOID)
{
  EFI_STATUS Status = VariablesFlush ();

  if (EFI_ERROR (Status)) {
    VariablesDiscard ();
  }
  return Status;
}

// The settings as stored, when the ABL didn't answer after a save: as
// MenuHostState reads them, with the charger screen and USB host from
// Before.
STATIC VOID
StoredWithout (
  IN CONST MENU_HOST_STATE *Before,
  OUT SETTINGS             *Stored
)
{
  SettingsLoad (Stored);
  Stored->OffmodeCharging = Before->Settings.OffmodeCharging;
  if (Stored->BootSource == BOOT_SOURCE_USB &&
      !(Before->Flags & OPENABL_MENU_STATE_USB_HOST)) {
    Stored->BootSource = BOOT_SOURCE_AUTO;
  }
}

SETTINGS_SAVE_RESULT
SettingsSave (
  IN CONST MENU_HOST *Host,
  IN CONST SETTINGS  *Want,
  OUT SETTINGS       *Stored
)
{
  MENU_HOST_STATE      Before;
  MENU_HOST_STATE      After;
  SETTINGS             Defaults;
  SETTINGS_VAR         Vars[SETTINGS_VARS];
  SETTINGS_VAR         DefaultVars[SETTINGS_VARS];
  BOOLEAN              AblModeSet = FALSE;
  BOOLEAN              Charger    = FALSE;
  SETTINGS_SAVE_RESULT Result     = SETTINGS_SAVED;
  CHAR8                Text[MODEL_LEN + 8];

  if (EFI_ERROR (MenuHostState (Host, &Before))) {
    return SETTINGS_NO_ABL;
  }
  SettingsDefault (&Defaults);
  ToVars (&Defaults, DefaultVars);
  ToVars (Want, Vars);
  for (UINTN Index = 0; Index < SETTINGS_VARS; Index++) {
    SETTINGS_VAR *Var = &Vars[Index];
    EFI_STATUS    Status;

    // A setting at its default isn't stored.
    if (SameVar (Var, &DefaultVars[Index])) {
      Var->Len = 0;
    }
    if (VariablesHolds (Var->Name, Var->Value, Var->Len)) {
      continue;
    }
    Status = VariablesPut (Var->Name, Var->Value, Var->Len);
    if (EFI_ERROR (Status)) {
      LogPrint (
        mLog, "setting %s failed: %s", Var->Name, STATUS_TEXT (Status)
      );
      Result = AsciiStrEq (Var->Name, SETTINGS_KEY_MODEL)
                 ? SETTINGS_MODEL_NOT_SAVED
                 : SETTINGS_NOT_SAVED;
      break;
    }
    AblModeSet = AblModeSet || AsciiStrEq (Var->Name, SETTINGS_KEY_ABL_MODE);
    Shown (Var, Text, sizeof (Text));
    LogPrint (mLog, "set %s = %s", Var->Name, Text);
  }
  // One write for all of them, also after a failure, so misc holds what
  // the settings read back.
  if (EFI_ERROR (Flush ()) && Result == SETTINGS_SAVED) {
    Result = SETTINGS_NOT_SAVED;
  }
  // The charger screen is Qualcomm's devinfo flag, set through the ABL.
  if (Result == SETTINGS_SAVED &&
      Want->OffmodeCharging != Before.Settings.OffmodeCharging) {
    EFI_STATUS Status = MenuHostAction (
      Host, Want->OffmodeCharging ? OPENABL_MENU_ACTION_CHARGER_SCREEN_ON
                                  : OPENABL_MENU_ACTION_CHARGER_SCREEN_OFF
    );

    if (EFI_ERROR (Status)) {
      LogPrint (
        mLog, "setting OffmodeCharging failed: %s", STATUS_TEXT (Status)
      );
      Result = SETTINGS_NOT_SAVED;
    } else {
      Charger = TRUE;
    }
  }
  // Read back what is stored, which also checks the charger screen.
  if (EFI_ERROR (MenuHostState (Host, &After))) {
    StoredWithout (&Before, Stored);
    if (Charger) {
      LogLine (mLog, "OffmodeCharging: the ABL didn't answer to check it");
      Result = SETTINGS_NOT_SAVED;
    }
  } else {
    *Stored = After.Settings;
  }
  // Show or hide the ABL's log right away, as AblMode is now stored.
  if (AblModeSet) {
    if (Stored->AblMode == ABL_MODE_VERBOSE) {
      AblConsoleShow ();
    } else {
      AblConsoleHide ();
    }
  }
  if (Charger && Result == SETTINGS_SAVED) {
    if (After.Settings.OffmodeCharging != Want->OffmodeCharging) {
      LogLine (mLog, "setting OffmodeCharging failed: devinfo kept it");
      Result = SETTINGS_NOT_SAVED;
    } else {
      LogPrint (
        mLog, "set OffmodeCharging = %u (devinfo)",
        Want->OffmodeCharging ? 1 : 0
      );
    }
  }
  return Result;
}

BOOLEAN
SettingsFlagGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
)
{
  return VariablesGet (Name, Out);
}

BOOLEAN
SettingsFlagIs (
  IN CONST CHAR8 *Name,
  IN UINT8        Value
)
{
  UINT8 Stored;

  return Byte (Name, &Stored) && Stored == Value;
}

EFI_STATUS
SettingsFlagSet (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Size
)
{
  // A put that changes nothing leaves nothing to write.
  EFI_STATUS Status = VariablesPut (Name, Data, Size);

  if (EFI_ERROR (Status)) {
    LogPrint (mLog, "setting %s failed: %s", Name, STATUS_TEXT (Status));
    return Status;
  }
  return Flush ();
}

VOID
SettingsPreload (VOID)
{
  VariablesLoad (LOG_QUIET);
}

#endif
