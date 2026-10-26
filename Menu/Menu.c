/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Menu - the menu, the settings it stores, and its commands.
*/

#include "Menu/Menu.h"

#include "Base/Memory.h"
#include "Base/Buffer.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Core/SysInfoText.h"
#include "Core/UsbHostRegs.h"
#include "Efi/Abl.h"
#include "Efi/Efi.h"
#include "Efi/DefaultFiles.h"
#include "Efi/Display.h"
#include "Efi/Input.h"
#include "Efi/Models.h"
#include "Efi/Qcom.h"
#include "Efi/Quirks.h"
#include "Efi/Variables.h"
#include "Efi/SysInfo.h"
#include "Efi/Uninstall.h"
#include "Efi/UsbHost.h"
#include "Efi/Files.h"
#include "Ui/Lang.h"
#include "Ui/Ui.h"
#include "View/View.h"
#ifdef OABL_STANDARD_BOOT
#include "Boot/BootEntries.h"
#endif

extern CONST UINT8 gOablAssetFont[];
extern CONST UINTN gOablAssetFontSize;

// Which model the cached DeviceModelRotation belongs to.
#define KEY_ROTATION_FOR SETTINGS_KEY_MODEL_ROTATION_FOR
#define NONE_U16         0xFFFFu

// Settings variables

BOOLEAN
MenuVarGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
)
{
  return VariablesGet (Name, Out);
}

EFI_STATUS
MenuVarPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Size
)
{
  return VariablesPut (Name, Data, Size);
}

BOOLEAN
MenuVarIs (
  IN CONST CHAR8 *Name,
  IN UINT8        Value
)
{
  OABL_BUFFER Stored = {0};
  BOOLEAN     Is =
    MenuVarGet (Name, &Stored) && Stored.Len == 1 && Stored.Data[0] == Value;

  BufferFree (&Stored);
  return Is;
}

// Like MenuVarPut, but only logs a failure.
STATIC VOID
VarSet (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data,
  IN UINTN        Size
)
{
  EFI_STATUS Status = MenuVarPut (Name, Data, Size);

  if (EFI_ERROR (Status)) {
    MenuLog ("SetVariable %s failed: %s", Name, MENU_STATUS (Status));
  }
}

VOID
MenuFlush (VOID)
{
  EFI_STATUS Status;

  if (QcomFlushVariables (&Status) &&
      (EFI_ERROR (Status) || Status == EFI_WARN_WRITE_FAILURE)) {
    MenuLog (
      "%s%s",
      QcomHasVariableServices () ? "FlushVariableNV: "
                                 : "the settings into devinfo: ",
      MENU_STATUS (Status)
    );
  }
}

// The settings Save stores. MenuSavePrefs saves the display preferences.
STATIC CONST CHAR8 *CONST mAblKeys[7] = {
  SETTINGS_KEY_BOOT_MODE,        SETTINGS_KEY_ABL_MODE,
  SETTINGS_KEY_ALT_VOLUME_UP,    SETTINGS_KEY_BOOT_SOURCE,
  SETTINGS_KEY_OFFMODE_CHARGING, SETTINGS_KEY_MODEL,
  SETTINGS_KEY_CUSTOM_LOGO,
};

STATIC BOOLEAN
AblKey (IN CONST CHAR8 *Key)
{
  for (UINTN Index = 0; Index < sizeof (mAblKeys) / sizeof (*mAblKeys);
       Index++) {
    if (AsciiStrEq (Key, mAblKeys[Index])) {
      return TRUE;
    }
  }
  return FALSE;
}

// Formats a setting's value for the log.
STATIC VOID
Shown (
  IN CONST CHAR8 *Key,
  IN CONST UINT8 *Value,
  IN UINTN        Size,
  OUT CHAR8      *Out,
  IN UINTN        OutSize
)
{
  if (AsciiStrEq (Key, SETTINGS_KEY_MODEL)) {
    CHAR8 *Text;
    if (!Size) {
      AsciiStrCopy (Out, OutSize, "none");
      return;
    }
    Text = Utf8Lossy (Value, Size);
    SPrint (Out, OutSize, "\"%s\"", Text ? Text : "");
    FreePool (Text);
    return;
  }
  if (Size) {
    SPrint (Out, OutSize, "%u", Value[0]);
  } else {
    AsciiStrCopy (Out, OutSize, "");
  }
}

STATIC BOOLEAN
StoredSettingGet (
  IN VOID         *Context OPTIONAL,
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
)
{
  (VOID)Context;
  return MenuVarGet (Name, Out);
}

// Reads and deletes BootFailure, why the boot before a restart into
// fastboot found nothing. Never returns NO_USB, which would make the menu
// restart for it again.
STATIC UINT8
CarriedResult (VOID)
{
  OABL_BUFFER Stored = {0};
  UINT8       Result;

  if (!MenuVarGet (SETTINGS_KEY_BOOT_FAILURE, &Stored) || Stored.Len != 1) {
    BufferFree (&Stored);
    return MENU_START_RESULT_NONE;
  }
  Result = Stored.Data[0];
  BufferFree (&Stored);
  MenuVarPut (SETTINGS_KEY_BOOT_FAILURE, NULL, 0);
  MenuFlush ();
  MenuLog ("the boot before the restart: %u", Result);
  return Result >= MENU_START_RESULT_NOTHING &&
             Result <= MENU_START_RESULT_FAILED
           ? Result
           : MENU_START_RESULT_NONE;
}

BOOLEAN
MenuStoredSettings (
  IN CONST MENU_HOST_STATE *State,
  IN CONST SETTINGS_CAPS   *Caps,
  OUT SETTINGS             *Out
)
{
  SETTINGS Defaults;

  if (!SettingsInVars (StoredSettingGet, NULL)) {
    return FALSE;
  }
  SettingsDefault (&Defaults);
  SettingsFromVars (&Defaults, StoredSettingGet, NULL, Out);
  // An ABL built without USB host reads the USB boot source as Auto.
  if (Out->BootSource == BOOT_SOURCE_USB && !Caps->UsbHost) {
    Out->BootSource = BOOT_SOURCE_AUTO;
  }
  // The charger screen setting is Qualcomm's devinfo flag.
  Out->OffmodeCharging = State->OffmodeCharging != 0;
  return TRUE;
}

// Reads a two-byte little-endian variable. Fails if it isn't two bytes.
STATIC BOOLEAN
Word (
  IN CONST CHAR8 *Name,
  OUT UINT16     *Out
)
{
  OABL_BUFFER Stored = {0};
  BOOLEAN     Ok     = MenuVarGet (Name, &Stored) && Stored.Len == 2;

  if (Ok) {
    *Out = ReadLe16 (Stored.Data);
  }
  BufferFree (&Stored);
  return Ok;
}

VOID
MenuLoadPrefs (
  IN CONST CHAR8 *Model,
  OUT UINT16     *Rotation,
  OUT UINT16     *ModelRotation,
  OUT UINT8      *Language
)
{
  OABL_BUFFER Owner  = {0};
  OABL_BUFFER Stored = {0};
  UINT16      Degrees;

  *Rotation =
    Word (SETTINGS_KEY_ROTATION, &Degrees) &&
        (Degrees == 0 || Degrees == 90 || Degrees == 180 || Degrees == 270)
      ? Degrees
      : ROTATION_AUTO;
  *ModelRotation = ROTATION_NONE;
  if (Model[0] && MenuVarGet (KEY_ROTATION_FOR, &Owner) &&
      Utf8Valid (Owner.Data, Owner.Len) &&
      TextEq (
        (OABL_TEXT){(CONST CHAR8 *)Owner.Data, Owner.Len}, TextOf (Model)
      ) &&
      Word (SETTINGS_KEY_MODEL_ROTATION, &Degrees) && Degrees != NONE_U16 &&
      Degrees % 90 == 0) {
    *ModelRotation = Degrees;
  }
  BufferFree (&Owner);
  *Language = MenuVarGet (SETTINGS_KEY_LANGUAGE, &Stored) && Stored.Len == 1 &&
                  Stored.Data[0] < SETTINGS_LANGUAGES
                ? Stored.Data[0]
                : LANG_EN;
  BufferFree (&Stored);
}

VOID
MenuSavePrefs (IN CONST SETTINGS *Settings)
{
  UINT8 Bytes[2];

  WriteLe16 (
    Bytes, Settings->Rotation == ROTATION_AUTO ? NONE_U16 : Settings->Rotation
  );
  VarSet (SETTINGS_KEY_ROTATION, Bytes, 2);
  VarSet (
    KEY_ROTATION_FOR, (CONST UINT8 *)Settings->Model,
    AsciiStrLen (Settings->Model)
  );
  WriteLe16 (Bytes, Settings->ModelRotation);
  VarSet (SETTINGS_KEY_MODEL_ROTATION, Bytes, 2);
  VarSet (SETTINGS_KEY_LANGUAGE, &Settings->Language, 1);
  MenuFlush ();
}

SETTINGS_CAPS
MenuCapsOf (IN CONST MENU_HOST_STATE *State)
{
  return (SETTINGS_CAPS){
    .Android       = TRUE,
    .UsbHost       = (State->Flags & OPENABL_MENU_STATE_USB_HOST) != 0,
    .CfwOnInternal = UninstallCfwOnInternal (QuirksChipId (), gMenuEfiLog),
    .Embedded      = TRUE,
    .AlternateSlot = (State->Flags & OPENABL_MENU_STATE_ALTERNATE_SLOT) != 0,
    .DeviceInfo    = TRUE,
    .Log           = TRUE,
  };
}

// Copies Text into Out, or "" when Text is NULL, and frees Text.
STATIC VOID
Take (
  OUT CHAR8 *Out,
  IN UINTN   OutSize,
  IN CHAR8  *Text OPTIONAL
)
{
  AsciiStrCopy (Out, OutSize, Text ? Text : "");
  FreePool (Text);
}

// Fills the info card. A value that isn't known is left empty, and the view
// draws it in the current language.
STATIC VOID
InfoOf (OUT DEVICE_INFO *Info)
{
  *Info = (DEVICE_INFO){0};
  Take (
    Info->Firmware, sizeof (Info->Firmware), Utf16ToStr (gST->FirmwareVendor)
  );
  Take (Info->Soc, sizeof (Info->Soc), SysInfoSoc ());
  Take (Info->Ram, sizeof (Info->Ram), SysInfoRam ());
  Take (Info->Storage, sizeof (Info->Storage), SysInfoStorage ());
  Take (Info->SdCard, sizeof (Info->SdCard), SysInfoSdCard ());
}

VOID
MenuSettingsOf (
  IN CONST MENU_HOST_STATE *State,
  OUT SETTINGS             *Out
)
{
  SettingsDefault (Out);
  Out->BootMode    = State->BootMode == 1 ? BOOT_MODE_ANDROID : BOOT_MODE_LINUX;
  Out->AblMode     = State->AblMode == 1 ? ABL_MODE_VERBOSE : ABL_MODE_SILENT;
  Out->AltVolumeUp = State->AltVolumeUp != 0;
  switch (State->BootSource) {
  case MENU_HOST_BOOT_SOURCE_INTERNAL:
    Out->BootSource = BOOT_SOURCE_INTERNAL;
    break;
  case MENU_HOST_BOOT_SOURCE_USB:
    Out->BootSource = BOOT_SOURCE_USB;
    break;
  case MENU_HOST_BOOT_SOURCE_SDCARD:
    Out->BootSource = BOOT_SOURCE_SD;
    break;
  default:
    Out->BootSource = BOOT_SOURCE_AUTO;
  }
  Out->OffmodeCharging = State->OffmodeCharging != 0;
}

// The menu

typedef struct _MENU_STATE {
  MENU_HOST Host;
  // The settings as stored, plus the display preferences.
  SETTINGS Stored;
  // The settings are in variables, so the menu can change them.
  BOOLEAN       Writable;
  SETTINGS_CAPS Caps;
  // Started on the model list after the restart for it. USB host is up, so
  // the USB boot source can be scanned.
  BOOLEAN StartModels;
  // When to restart into recovery after an uninstall, in ms. 0 for never.
  UINT64 RestartAt;
  // Why the last Start Linux didn't boot (MENU_START_RESULT_*).
  UINT8 CameBack;
  // For the device info screen.
  CHAR8 AblVersion[64];
  UINTN DisplayW, DisplayH;
} MENU_STATE;

// How long the uninstall's result stays on screen before recovery starts.
#define UNINSTALL_RESTART_MS 5000

// Sets up a menu run and fills the info card in Info.
STATIC EFI_STATUS
MenuNew (
  OUT MENU_STATE     *M,
  IN CONST MENU_HOST *Host,
  IN BOOLEAN          StartModels,
  IN UINT8            CameBack,
  OUT DEVICE_INFO    *Info
)
{
  MENU_HOST_STATE State;
  EFI_STATUS      Status = MenuHostState (Host, &State);
  UINT8           Carried;
  CHAR8          *Version;

  if (EFI_ERROR (Status)) {
    return Status;
  }
  *M          = (MENU_STATE){.Host = *Host, .StartModels = StartModels};
  M->Caps     = MenuCapsOf (&State);
  M->Writable = MenuStoredSettings (&State, &M->Caps, &M->Stored);
  if (!M->Writable) {
    MenuLog (
      "the settings aren't in UEFI variables (they couldn't be written): "
      "shown, not changed"
    );
    MenuSettingsOf (&State, &M->Stored);
  }
  MenuLoadPrefs (
    M->Stored.Model, &M->Stored.Rotation, &M->Stored.ModelRotation,
    &M->Stored.Language
  );
  LangSet (M->Stored.Language);
  Version = MenuHostText (State.Version, sizeof (State.Version));
  AsciiStrCopy (M->AblVersion, sizeof (M->AblVersion), Version ? Version : "");
  FreePool (Version);
  Carried     = CarriedResult ();
  M->CameBack = CameBack == MENU_START_RESULT_NONE ? Carried : CameBack;
  InfoOf (Info);
  return EFI_SUCCESS;
}

// Reads the settings and the ABL's state again, keeping the display
// preferences.
STATIC EFI_STATUS
Refresh (IN OUT MENU_STATE *M)
{
  MENU_HOST_STATE State;
  SETTINGS        Base;
  EFI_STATUS      Status = MenuHostState (&M->Host, &State);

  if (EFI_ERROR (Status)) {
    return Status;
  }
  M->Caps     = MenuCapsOf (&State);
  M->Writable = MenuStoredSettings (&State, &M->Caps, &Base);
  if (!M->Writable) {
    MenuSettingsOf (&State, &Base);
  }
  Base.ModelRotation = AsciiStrEq (Base.Model, M->Stored.Model)
                         ? M->Stored.ModelRotation
                         : ROTATION_NONE;
  Base.Rotation      = M->Stored.Rotation;
  Base.Language      = M->Stored.Language;
  M->Stored          = Base;
  return EFI_SUCCESS;
}

// Stores the changed settings and the display preferences, then checks
// what the variables actually hold. Returns an error message, or NULL.
STATIC CONST CHAR8 *
Save (
  IN OUT MENU_STATE *M,
  IN CONST SETTINGS *Want
)
{
  SETTINGS_VAR Changes[SETTINGS_VARS];
  UINTN        Count = SettingsChangedVars (&M->Stored, Want, Changes);
  UINTN        Kept  = 0;
  CHAR8        Text[MODEL_LEN + 8];

  for (UINTN Index = 0; Index < Count; Index++) {
    if (AblKey (Changes[Index].Name)) {
      Changes[Kept++] = Changes[Index];
    }
  }
  Count = Kept;
  if (Count && !M->Writable) {
    MenuLog (
      "the settings aren't in UEFI variables: they can't be changed here"
    );
    return gStrings->AblSettingFailed;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    CONST CHAR8 *Key   = Changes[Index].Name;
    CONST UINT8 *Value = Changes[Index].Value;
    UINTN        Len   = Changes[Index].Len;
    EFI_STATUS   Status;

    // The charger screen is Qualcomm's devinfo flag.
    if (AsciiStrEq (Key, SETTINGS_KEY_OFFMODE_CHARGING)) {
      UINT32 Action = Len && Value[0] == 1
                        ? OPENABL_MENU_ACTION_CHARGER_SCREEN_ON
                        : OPENABL_MENU_ACTION_CHARGER_SCREEN_OFF;
      Status        = MenuHostAction (&M->Host, Action);
      if (EFI_ERROR (Status)) {
        MenuLog ("setting %s failed: %s", Key, MENU_STATUS (Status));
        Refresh (M);
        return gStrings->AblSettingFailed;
      }
      Shown (Key, Value, Len, Text, sizeof (Text));
      MenuLog ("set %s = %s (devinfo)", Key, Text);
      continue;
    }
    Status = MenuVarPut (Key, Value, Len);
    if (EFI_ERROR (Status)) {
      MenuLog ("setting %s failed: %s", Key, MENU_STATUS (Status));
      Refresh (M);
      return AsciiStrEq (Key, SETTINGS_KEY_MODEL) ? gStrings->AblModelFailed
                                                  : gStrings->AblSettingFailed;
    }
    // Show or hide the ABL's log right away.
    if (AsciiStrEq (Key, SETTINGS_KEY_ABL_MODE)) {
      if (Len && Value[0] == 1) {
        AblConsoleShow ();
      } else {
        AblConsoleHide ();
      }
    }
    Shown (Key, Value, Len, Text, sizeof (Text));
    MenuLog ("set %s = %s", Key, Text);
  }
  // MenuSavePrefs also flushes the settings written above.
  M->Stored.Rotation = Want->Rotation;
  AsciiStrCopy (M->Stored.Model, sizeof (M->Stored.Model), Want->Model);
  M->Stored.ModelRotation = Want->ModelRotation;
  M->Stored.Language      = Want->Language;
  MenuSavePrefs (Want);
  if (EFI_ERROR (Refresh (M))) {
    return gStrings->AblNotAnswering;
  }
  Count = SettingsChangedVars (&M->Stored, Want, Changes);
  for (UINTN Index = 0; Index < Count; Index++) {
    if (AblKey (Changes[Index].Name)) {
      return gStrings->AblSettingFailed;
    }
  }
  return NULL;
}

STATIC BOOLEAN
ChipBoard (OUT USB_HOST_BOARD *Board)
{
  QCOM_CHIP Chip;
  UINT32    Id;

  if (!QcomChip (&Chip)) {
    return FALSE;
  }
  Id = EFI_ERROR (Chip.Id.Status) ? 0 : Chip.Id.Value;
  return UsbHostBoardOf (Id, Chip.Name, Board);
}

// Returns TRUE if USB host can come up without a restart, with fastboot
// paused. This is an experiment, turned on by UsbHostLive.
STATIC BOOLEAN
UsbLive (IN CONST MENU_STATE *M)
{
  USB_HOST_BOARD Board;

  return M->Caps.UsbHost && MenuVarIs (SETTINGS_KEY_USB_HOST_LIVE, 1) &&
         ChipBoard (&Board);
}

STATIC EFI_STATUS
Act (
  IN MENU_STATE *M,
  IN UINT32      Action
)
{
  EFI_STATUS Status;

  MenuLog ("action %s", MenuHostActionName (Action));
  Status = MenuHostAction (&M->Host, Action);
  MenuLog ("%s -> %s", MenuHostActionName (Action), MENU_STATUS (Status));
  return Status;
}

// Restarts for the model list. Fastboot holds the USB controller, so the
// ABL scans the USB boot source in the next boot, before starting fastboot.
// Returns only if the restart didn't happen.
STATIC EFI_STATUS
RestartToScan (IN MENU_STATE *M)
{
  STATIC CONST UINT8 One = 1;
  EFI_STATUS         Status;

  if (!M->Writable) {
    return EFI_UNSUPPORTED;
  }
  Status = MenuVarPut (SETTINGS_KEY_SHOW_MODEL_MENU, &One, 1);
  if (EFI_ERROR (Status)) {
    MenuLog ("setting ShowModelMenu failed: %s", MENU_STATUS (Status));
    return Status;
  }
  MenuFlush ();
  MenuLog ("set ShowModelMenu = 1");
  return Act (M, OPENABL_MENU_ACTION_START);
}

// Brings USB host up or down for the menu, with fastboot's gadget paused
// while it is up.
STATIC EFI_STATUS
UsbHost (
  IN MENU_STATE *M,
  IN BOOLEAN     On
)
{
  EFI_STATUS Status =
    On ? MenuLiveUsbHostUp (&M->Host) : MenuLiveUsbHostDown (&M->Host);

  MenuLog ("USB host %s -> %s", On ? "up" : "down", MENU_STATUS (Status));
  return Status;
}

STATIC VOID
ScanDrives (
  IN MENU_STATE      *M,
  OUT SCANNED_MODELS *Found
)
{
  VOLUMES_HANDLE_LIST Drives = {0};

  QuirksPrepareStorage (QuirksChipId (), gMenuEfiLog);
  VolumesAllowed (M->Stored.BootSource, M->Caps.CfwOnInternal, FALSE, &Drives);
  ModelsScan (Drives.Items, Drives.Count, gMenuEfiLog, Found);
  MenuLog (
    "scan: %llu models on %llu drive(s)", (UINT64)Found->Count,
    (UINT64)Drives.Count
  );
  VecFree (&Drives);
}

// Finds the models on the allowed drives. For the USB boot source, UsbNow
// brings USB host up now if it can. Otherwise returns EFI_NOT_READY, and
// the ABL scans after a restart.
STATIC EFI_STATUS
FindModels (
  IN MENU_STATE      *M,
  IN BOOLEAN          UsbNow,
  OUT SCANNED_MODELS *Found
)
{
  *Found = (SCANNED_MODELS){0};
  if (M->Stored.BootSource == BOOT_SOURCE_USB && !M->StartModels) {
    if (!(UsbNow && UsbLive (M))) {
      return EFI_NOT_READY;
    }
    if (UsbHost (M, TRUE) != EFI_SUCCESS) {
      return EFI_NOT_READY;
    }
    ScanDrives (M, Found);
    // A failure only means fastboot didn't come back, and is logged.
    UsbHost (M, FALSE);
    return EFI_SUCCESS;
  }
  ScanDrives (M, Found);
  return EFI_SUCCESS;
}

STATIC CONST SCANNED_MODEL *
ModelNamed (
  IN CONST SCANNED_MODELS *Found,
  IN CONST CHAR8          *Name
)
{
  for (UINTN Index = 0; Index < Found->Count; Index++) {
    if (AsciiStrEq (Found->Items[Index].Name, Name)) {
      return &Found->Items[Index];
    }
  }
  return NULL;
}

STATIC UINT16
RotationOfModel (IN CONST SCANNED_MODEL *Model OPTIONAL)
{
  return Model && Model->HasRotation ? Model->Rotation : ROTATION_NONE;
}

// Looks up the selected model's panel rotation the first time the menu runs
// with it, so Auto is right from the first screen. The portrait panel guess
// suits only some devices. The result is saved even when the devicetree has
// none, so this only repeats if the scan failed (USB boot source).
STATIC VOID
LearnModelRotation (IN OUT MENU_STATE *M)
{
  SCANNED_MODELS Found;
  EFI_STATUS     Status;
  CHAR8          Text[8];

  if (!M->Stored.Model[0]) {
    return;
  }
  {
    OABL_BUFFER Owner = {0};
    BOOLEAN     Same  = MenuVarGet (KEY_ROTATION_FOR, &Owner) &&
                        TextEq (
                          (OABL_TEXT){(CONST CHAR8 *)Owner.Data, Owner.Len},
                          TextOf (M->Stored.Model)
                        );
    BufferFree (&Owner);
    if (Same) {
      return;
    }
  }
  Status = FindModels (M, FALSE, &Found);
  if (EFI_ERROR (Status)) {
    MenuLog (
      "couldn't look up the model's panel rotation: %s", MENU_STATUS (Status)
    );
    return;
  }
  M->Stored.ModelRotation =
    RotationOfModel (ModelNamed (&Found, M->Stored.Model));
  MenuSavePrefs (&M->Stored);
  if (M->Stored.ModelRotation == ROTATION_NONE) {
    AsciiStrCopy (Text, sizeof (Text), "none");
  } else {
    AsciiStrFromU64 (Text, sizeof (Text), M->Stored.ModelRotation);
  }
  MenuLog ("panel rotation of %s: %s", M->Stored.Model, Text);
  ModelsFree (&Found);
}

// Builds the model list for the UI, and saves the selected model's rotation
// for the next start. Caller frees *Out.
STATIC EFI_STATUS
Scan (
  IN OUT MENU_STATE *M,
  OUT MODEL        **Out,
  OUT UINTN         *Count
)
{
  SCANNED_MODELS       Found;
  EFI_STATUS           Status = FindModels (M, TRUE, &Found);
  CONST SCANNED_MODEL *Selected;

  if (EFI_ERROR (Status)) {
    return Status;
  }
  Selected = M->Stored.Model[0] ? ModelNamed (&Found, M->Stored.Model) : NULL;
  if (Selected && M->Stored.ModelRotation != RotationOfModel (Selected)) {
    M->Stored.ModelRotation = RotationOfModel (Selected);
    MenuSavePrefs (&M->Stored);
  }
  *Count = Found.Count;
  *Out   = AllocateZeroArray (Found.Count ? Found.Count : 1, sizeof (**Out));
  for (UINTN Index = 0; *Out && Index < Found.Count; Index++) {
    AsciiStrCopy (
      (*Out)[Index].Name, sizeof (*Out)[Index].Name, Found.Items[Index].Name
    );
    (*Out)[Index].Rotation = RotationOfModel (&Found.Items[Index]);
    AsciiStrCopy (
      (*Out)[Index].Source, sizeof (*Out)[Index].Source, gStrings->SrcAblScan
    );
  }
  if (!*Out) {
    *Count = 0;
  }
  ModelsFree (&Found);
  return EFI_SUCCESS;
}

#ifdef OABL_STANDARD_BOOT
// The drives Boot entries lists, and each row's drive and id.
STATIC BOOT_ENTRIES_VOLUMES mVolumes;
STATIC UINTN                mEntryVolume[MAX_ENTRIES];
STATIC CHAR8                mEntryIds[MAX_ENTRIES][256];

STATIC ENTRY_KIND
EntryKind (IN BOOT_ENTRY_SOURCE Source)
{
  switch (Source) {
  case BOOT_ENTRY_SOURCE_TYPE1:
    return ENTRY_KIND_BLS;
  case BOOT_ENTRY_SOURCE_UKI:
    return ENTRY_KIND_UKI;
  case BOOT_ENTRY_SOURCE_EXTLINUX:
    break;
  }
  return ENTRY_KIND_EXTLINUX;
}

// Adds a row for drive Volume of mVolumes, if there is room.
STATIC VOID
AddRow (
  IN OUT ENTRY_VIEW *Views,
  IN OUT UINTN      *Count,
  IN UINTN           Volume,
  IN CONST CHAR8    *Title,
  IN ENTRY_KIND      Kind,
  IN CONST CHAR8    *Version OPTIONAL,
  IN BOOLEAN         Bad,
  IN CONST CHAR8    *Id
)
{
  CONST BOOT_ENTRIES_VOLUME *V = &mVolumes.Items[Volume];

  if (*Count < MAX_ENTRIES) {
    ENTRY_VIEW *View = &Views[*Count];

    AsciiStrCopy (View->Title, sizeof (View->Title), Title);
    AsciiStrCopy (
      View->Version, sizeof (View->Version), Version ? Version : ""
    );
    AsciiStrCopy (View->Drive, sizeof (View->Drive), V->Name ? V->Name : "");
    View->Kind           = Kind;
    View->Removable      = V->Removable;
    View->Bad            = Bad;
    mEntryVolume[*Count] = Volume;
    AsciiStrCopy (mEntryIds[(*Count)++], sizeof (mEntryIds[0]), Id);
  }
}

// Lists what Start would try on the allowed drives, in its order: each
// drive's entries, then each drive's default file, then the first boot
// image. Only in Linux boot mode. USB drives are skipped unless USB
// host is up, since fastboot owns the controller.
STATIC VOID
FillEntries (
  IN MENU_STATE *M,
  IN OUT APP    *A
)
{
  STATIC ENTRY_VIEW   Views[MAX_ENTRIES];
  VOLUMES_HANDLE_LIST Drives = {0};
  UINTN               Count  = 0;
  BOOLEAN             Image  = FALSE;

  BootEntriesVolumesFree (&mVolumes);
  if (M->Stored.BootMode == BOOT_MODE_LINUX &&
      (M->Stored.BootSource != BOOT_SOURCE_USB || UsbHostIsUp ())) {
    QuirksPrepareStorage (QuirksChipId (), gMenuEfiLog);
    VolumesAllowed (
      M->Stored.BootSource, M->Caps.CfwOnInternal, FALSE, &Drives
    );
    BootEntriesDiscover (
      BytesOf (NULL, 0), TRUE, Drives.Items, Drives.Count, FALSE, &mVolumes
    );
  }
  for (UINTN Index = 0; Index < mVolumes.Count; Index++) {
    CONST BOOT_ENTRIES_VOLUME *V = &mVolumes.Items[Index];

    for (UINTN Index2 = 0; Index2 < V->Entries.Count; Index2++) {
      CONST BOOT_ENTRY *Entry = &V->Entries.Items[Index2];

      AddRow (
        Views, &Count, Index, Entry->Title ? Entry->Title : Entry->Id,
        EntryKind (Entry->Source), Entry->Version,
        BootEntryIsBad (Entry), Entry->Id
      );
    }
  }
  // Then the default files, which Start tries on each drive in turn.
  for (UINTN Index = 0; Index < mVolumes.Count; Index++) {
    for (UINTN Index2 = 0;
         Index2 < sizeof (gDefaultFilesPaths) / sizeof (*gDefaultFilesPaths);
         Index2++) {
      if (FilesExists (mVolumes.Items[Index].Fs, gDefaultFilesPaths[Index2])) {
        // The file name, such as BOOTAA64.EFI.
        AddRow (
          Views, &Count, Index,
          gDefaultFilesPaths[Index2] + sizeof ("\\EFI\\BOOT\\") - 1,
          ENTRY_KIND_DEFAULT_FILE, NULL, FALSE, MENU_PICK_DEFAULT_FILE
        );
        break;
      }
    }
  }
  // Start reaches a boot image only after every drive's entries and
  // default files.
  for (UINTN Index = 0; Index < mVolumes.Count && !Image; Index++) {
    for (UINTN Index2 = 0;
         Index2 < sizeof (gModelsBootImages) / sizeof (*gModelsBootImages) &&
         !Image;
         Index2++) {
      // A file that isn't a valid Android boot image is skipped.
      if (ModelsIsBootImage (
            mVolumes.Items[Index].Fs, gModelsBootImages[Index2]
          )) {
        // The path without its backslash, such as KERNEL.
        AddRow (
          Views, &Count, Index, gModelsBootImages[Index2] + 1,
          ENTRY_KIND_BOOT_IMAGE, NULL, FALSE, MENU_PICK_BOOT_IMAGE
        );
        Image = TRUE;
      }
    }
  }
  MenuLog ("Boot entries: %llu", (UINT64)Count);
  AppSetEntries (A, Views, Count);
  VecFree (&Drives);
}

// Stores row Index of Boot entries as the next Start's pick.
STATIC VOID
PickEntry (IN UINTN Index)
{
  CONST OABL_BUFFER *Dev  = &mVolumes.Items[mEntryVolume[Index]].Dev;
  OABL_BUFFER        Pick = {0};
  EFI_STATUS         Status;

  BufferAppendLe16 (&Pick, (UINT16)Dev->Len);
  BufferAppend (&Pick, Dev->Data, Dev->Len);
  BufferAppend (&Pick, mEntryIds[Index], AsciiStrLen (mEntryIds[Index]));
  Status = MenuVarPut (MENU_PICK_VAR, Pick.Data, Pick.Len);
  MenuFlush ();
  BufferFree (&Pick);
  MenuLog ("Boot entries: %s (%s)", mEntryIds[Index], MENU_STATUS (Status));
}
#endif

// Returns TRUE if Start Linux can boot without a restart. A USB drive also
// needs live USB host, since fastboot holds the controller.
STATIC BOOLEAN
StartsHere (IN CONST MENU_STATE *M)
{
  return M->Stored.BootMode == BOOT_MODE_LINUX &&
         (M->Stored.BootSource != BOOT_SOURCE_USB || UsbLive (M));
}

// Gets what the menu shows about the last Start Linux. Returns FALSE if
// there is nothing to show.
STATIC BOOLEAN
CameBackShown (
  IN CONST MENU_STATE *M,
  OUT BOOLEAN         *Ok,
  OUT BOOT_ERROR      *Error,
  OUT CONST CHAR8    **Detail
)
{
  CONST CHAR8 *What;

  *Ok     = FALSE;
  *Detail = "";
  switch (M->CameBack) {
  case MENU_START_RESULT_NOTHING:
    What   = "nothing to boot";
    *Error = BOOT_ERROR_NO_IMAGE;
    break;
  case MENU_START_RESULT_RETURNED:
    What = "the booted image returned";
    *Ok  = TRUE;
    break;
  case MENU_START_RESULT_NO_MODEL:
    What   = "the boot image needs a device model";
    *Error = BOOT_ERROR_MODEL_NOT_SET;
    break;
  case MENU_START_RESULT_FAILED:
    What    = "the boot image failed";
    *Error  = BOOT_ERROR_LOAD_FAILED;
    *Detail = gStrings->ErrBootImageFailed;
    break;
  case MENU_START_RESULT_NO_USB:
    // Only shown if the restart for it didn't happen.
    What   = "USB host didn't come up";
    *Error = BOOT_ERROR_NO_IMAGE;
    break;
  case MENU_START_RESULT_ANDROID_FAILED:
    What   = "Android didn't start";
    *Error = BOOT_ERROR_ANDROID_FAILED;
    break;
  default:
    return FALSE;
  }
  MenuLog ("Start Linux came back: %s", What);
  return TRUE;
}

// Device info screen

typedef struct _LINE_LIST {
  TEXT_LINE *Items;
  UINTN      Count, Capacity;
} LINE_LIST;

// Adds a row, unless Value is NULL or empty, and frees Value.
STATIC VOID
Push (
  IN OUT LINE_LIST *Rows,
  IN CONST CHAR8   *Label,
  IN CHAR8         *Value OPTIONAL
)
{
  if (Value && Value[0] && Rows->Count < Rows->Capacity) {
    Rows->Items[Rows->Count].Label = Label;
    AsciiStrCopy (
      Rows->Items[Rows->Count].Value, sizeof (Rows->Items[Rows->Count].Value),
      Value
    );
    Rows->Count++;
  }
  FreePool (Value);
}

STATIC VOID
VersionText (
  IN OUT OABL_STRING *Text,
  IN UINT32           Version
)
{
  StringPrint (Text, "%u.%u", Version >> 16, Version & 0xFFFF);
}

// Returns "Text (raw size)", or NULL if Text is NULL. Frees Text.
STATIC CHAR8 *
Sized (
  IN CHAR8 *Text OPTIONAL,
  IN UINT64 Raw
)
{
  CHAR8 *Formatted;
  CHAR8 *Out;

  if (!Text) {
    return NULL;
  }
  Formatted = SysInfoFormatBytes (Raw);
  Out       = SPrintAlloc ("%s (%s)", Text, Formatted ? Formatted : "");
  FreePool (Formatted);
  FreePool (Text);
  return Out;
}

// Fills Rows with the device's details. Returns the number of rows.
STATIC UINTN
DeviceInfo (
  IN CONST MENU_STATE *M,
  OUT TEXT_LINE       *Rows,
  IN UINTN             Capacity
)
{
  LINE_LIST                           List = {Rows, 0, Capacity};
  QCOM_CHIP                           Chip;
  EFI_PLATFORMINFO_PLATFORM_INFO_TYPE Platform;
  EFI_PM_DEVICE_INFO_TYPE             Pmics[QCOM_PMICS_MAX];
  QCOM_CARDS                          Cards = {0};
  UINT64                              RamSize;
  UINT64                              StorageSize;
  UINT64                              CardSize;
  UINTN                               PmicCount;

  Push (&List, "SoC", SysInfoSoc ());
  if (QcomChip (&Chip)) {
    OABL_STRING Text = {0};
    StringAppend (&Text, Chip.Name);
    if (!EFI_ERROR (Chip.Version.Status)) {
      StringAppend (&Text, " v");
      VersionText (&Text, Chip.Version.Value);
    }
    StringPrint (
      &Text, " \xc2\xb7 id %u \xc2\xb7 raw 0x%x", Chip.Id.Value,
      Chip.RawId.Value
    );
    Push (&List, gStrings->InfoChip, StringTake (&Text));
  }
  if (QcomPlatform (&Platform)) {
    OABL_STRING  Text = {0};
    CONST CHAR8 *Name = SysInfoPlatformName ((UINT32)Platform.platform);
    if (Name) {
      StringAppend (&Text, Name);
    } else {
      StringPrint (&Text, "0x%x", (UINT32)Platform.platform);
    }
    StringAppend (&Text, " v");
    VersionText (&Text, Platform.version);
    StringPrint (&Text, " \xc2\xb7 subtype %u", Platform.subtype);
    if (Platform.OEMVariantID) {
      StringPrint (&Text, " \xc2\xb7 OEM %u", Platform.OEMVariantID);
    }
    Push (&List, gStrings->InfoPlatform, StringTake (&Text));
  }
  PmicCount = QcomPmics (Pmics);
  {
    OABL_STRING Text = {0};
    for (UINTN Index = 0; Index < PmicCount; Index++) {
      StringPrint (
        &Text, "%s0x%x %u.%u", Index ? ", " : "",
        (UINT32)Pmics[Index].PmicModel, Pmics[Index].PmicAllLayerRevision,
        Pmics[Index].PmicMetalRevision
      );
    }
    Push (&List, gStrings->InfoPmic, StringTake (&Text));
  }
  SysInfoRawSizes (&RamSize, &StorageSize, &CardSize);
  Push (&List, gStrings->InfoRam, Sized (SysInfoRam (), RamSize));
  Push (&List, gStrings->InfoStorage, Sized (SysInfoStorage (), StorageSize));
  QcomCards (&Cards);
  {
    // UFS has one card per LUN. List the device once.
    OABL_STRING Text = {0};
    OABL_STRING Last = {0};
    BOOLEAN     Any  = FALSE;
    for (UINTN Index = 0; Index < Cards.Count; Index++) {
      OABL_STRING      One  = {0};
      CONST QCOM_CARD *Card = &Cards.Items[Index];
      if (Card->Removable) {
        continue;
      }
      StringPrint (&One, "%s \xc2\xb7 ", Card->Kind ? Card->Kind : "");
      if (!Card->Inquiry || !Card->Inquiry[0]) {
        StringPrint (&One, "mfr 0x%x", Card->MfrId);
      } else {
        StringAppend (&One, Card->Inquiry);
      }
      if (!Any || !AsciiStrEq (StringGet (&One), StringGet (&Last))) {
        StringPrint (&Text, "%s%s", Any ? ", " : "", StringGet (&One));
        StringClear (&Last);
        StringAppend (&Last, StringGet (&One));
      }
      Any = TRUE;
      StringFree (&One);
    }
    StringFree (&Last);
    Push (&List, gStrings->InfoStorageChip, StringTake (&Text));
  }
  QcomCardsFree (&Cards);
  {
    CHAR8 *Sd = Sized (SysInfoSdCard (), CardSize);
    Push (
      &List, gStrings->SdCard, Sd ? Sd : AsciiStrDup (gStrings->NotInserted)
    );
  }
  if (M->DisplayW > 0) {
    Push (
      &List, gStrings->InfoDisplay,
      SPrintAlloc (
        "%llu \xc3\x97 %llu", (UINT64)M->DisplayW, (UINT64)M->DisplayH
      )
    );
  }
  {
    CHAR8 *Panel = SysInfoPanel (M->DisplayW, M->DisplayH);
    Push (
      &List, gStrings->InfoPanel,
      Panel ? Panel : AsciiStrDup (gStrings->Unknown)
    );
  }
  // XBL's DSI panel detection, as it hands it to the kernel.
  Push (&List, "DSI", QcomDisplayPanelConfig ());
  Push (&List, gStrings->InfoAndroid, SysInfoAndroidBuild ());
  Push (
    &List, gStrings->InfoModel,
    AsciiStrDup (M->Stored.Model[0] ? M->Stored.Model : gStrings->NotSet)
  );
  Push (
    &List, gStrings->InfoBootloader,
    SPrintAlloc ("OpenABL %s", M->AblVersion[0] ? M->AblVersion : MENU_VERSION)
  );
  {
    OABL_STRING Text     = {0};
    CHAR8      *Firmware = Utf16ToStr (gST->FirmwareVendor);
    StringAppend (&Text, Firmware ? Firmware : "");
    FreePool (Firmware);
    StringAppendChar (&Text, ' ');
    VersionText (&Text, gST->FirmwareRevision);
    Push (&List, gStrings->InfoUefi, StringTake (&Text));
  }
  return List.Count;
}

// Log screen

// The ABL keeps the last 32 KiB of its log.
#define LOG_MAX   (32u << 10)
#define LOG_LINES 400

// Splits the log into its newest LOG_LINES non-blank lines. The first may
// be cut short by the ABL's buffer. Returns the number of lines.
STATIC UINTN
LogLines (
  IN CONST OABL_BUFFER *Raw,
  OUT CHAR8           **Lines
)
{
  CHAR8    *Text = Utf8Lossy (Raw->Data, Raw->Len);
  OABL_TEXT Rest;
  OABL_TEXT Piece;
  BOOLEAN   Done  = FALSE;
  UINTN     Count = 0;

  if (!Text) {
    return 0;
  }
  Rest = TextOf (Text);
  while (TextNextSplit (&Rest, '\n', &Done, &Piece)) {
    while (Piece.Len && Piece.Ptr[Piece.Len - 1] == '\r') {
      Piece.Len--;
    }
    if (!Piece.Len) {
      continue;
    }
    if (Count == LOG_LINES) {
      FreePool (Lines[0]);
      for (UINTN Index = 1; Index < Count; Index++) {
        Lines[Index - 1] = Lines[Index];
      }
      Count--;
    }
    Lines[Count] = TextDup (Piece);
    if (Lines[Count]) {
      Count++;
    }
  }
  FreePool (Text);
  return Count;
}

// Debug section

#ifdef OABL_DEBUG
// Reads the Debug section's switches. A missing variable means the build's
// default.
STATIC EXPERIMENTS
Experiments (VOID)
{
  OABL_BUFFER Stored = {0};
  UINT8       Quiesce =
    MenuVarGet (SETTINGS_KEY_DISPLAY_QUIESCE, &Stored) && Stored.Len
      ? Stored.Data[0]
      : 0;

  BufferFree (&Stored);
  return (EXPERIMENTS){
    .UsbLive       = MenuVarIs (SETTINGS_KEY_USB_HOST_LIVE, 1),
    .Quiesce       = Quiesce & 1,
    .PanelLinesOff = (Quiesce & 2) != 0,
  };
}

// Stores and flushes the Debug section's switches.
STATIC EFI_STATUS
SaveExperiments (IN EXPERIMENTS Want)
{
  STATIC CONST UINT8 One     = 1;
  UINT8              Quiesce = (UINT8)(Want.Quiesce | Want.PanelLinesOff << 1);
  EFI_STATUS         Status =
    MenuVarPut (SETTINGS_KEY_USB_HOST_LIVE, &One, Want.UsbLive ? 1 : 0);

  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = MenuVarPut (SETTINGS_KEY_DISPLAY_QUIESCE, &Quiesce, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  MenuFlush ();
  MenuLog (
    "experiments: live USB host %s, display parked %s, panel lines off %s",
    Want.UsbLive ? "on" : "off", Want.Quiesce ? "on" : "off",
    Want.PanelLinesOff ? "on" : "off"
  );
  return EFI_SUCCESS;
}
#endif

// Commands

// Returns TRUE if Start Linux may boot without a restart. Only from
// fastboot's menu. Android, the alternate Volume Up boot, and a USB boot
// on an ABL without USB host need a restart.
STATIC BOOLEAN
MayStartHere (IN MENU_STATE *M)
{
  MENU_HOST_STATE State;
  BOOLEAN         Ok;

  Ok = gFastbootUp && !EFI_ERROR (MenuHostState (&M->Host, &State)) &&
       State.BootMode == 0 && !State.AltVolumeBoot &&
       (State.BootSource != MENU_HOST_BOOT_SOURCE_USB ||
        (State.Flags & OPENABL_MENU_STATE_USB_HOST));
  MenuLog ("Start Linux in this boot: %s", Ok ? "yes" : "no, a restart");
  return Ok;
}

// Static, as the app state is large (log and device info lines).
STATIC APP mApp;

// Runs a command. Returns TRUE to leave the menu, for Start Linux to boot
// without a restart.
STATIC BOOLEAN
RunCommand (
  IN OUT MENU_STATE *M,
  IN COMMAND         Command
)
{
  switch (Command.Kind) {
  case COMMAND_NONE:
    break;
  // Where possible, leave the menu with the start frame up and boot without
  // a restart. Otherwise START restarts and boots, and the host only
  // returns if it couldn't.
  case COMMAND_BOOT:
  case COMMAND_BOOT_ENTRY: {
    EFI_STATUS   Status;
    CONST CHAR8 *Args[1];
    CHAR8        Why[192];

#ifdef OABL_STANDARD_BOOT
    if (Command.Kind == COMMAND_BOOT_ENTRY &&
        (UINTN)Command.Arg < mApp.EntryCount) {
      PickEntry ((UINTN)Command.Arg);
    }
#endif
    if (StartsHere (M) && MayStartHere (M)) {
      // Shut down as when the ABL asks the menu to leave.
      return TRUE;
    }
    Status  = Act (M, OPENABL_MENU_ACTION_START);
#ifdef OABL_STANDARD_BOOT
    // The restart didn't happen: don't leave the pick for a later Start.
    MenuVarPut (MENU_PICK_VAR, NULL, 0);
    MenuFlush ();
#endif
    Args[0] = MENU_STATUS (Status);
    LangFill (Why, sizeof (Why), gStrings->AblRestartFailed, Args, 1);
    AppOnBootResult (&mApp, FALSE, BOOT_ERROR_LOAD_FAILED, Why);
    break;
  }
  case COMMAND_SAVE_SETTINGS: {
    SETTINGS     Want    = mApp.Settings;
    SETTINGS     Was     = M->Stored;
    CONST CHAR8 *Failure = Save (M, &Want);
    AppSetSettings (&mApp, &M->Stored);
    AppSetCaps (&mApp, &M->Caps);
#ifdef OABL_STANDARD_BOOT
    // Only these change what Start would try.
    if (M->Stored.BootMode != Was.BootMode ||
        M->Stored.BootSource != Was.BootSource) {
      FillEntries (M, &mApp);
    }
#endif
    if (Failure) {
      AppOnFailed (&mApp, Failure);
    }
    break;
  }
  case COMMAND_SCAN_MODELS: {
    MODEL     *Models = NULL;
    UINTN      Count  = 0;
    EFI_STATUS Status = Scan (M, &Models, &Count);

    if (!EFI_ERROR (Status)) {
      AppOnModels (&mApp, Models, Count);
    } else if (Status == EFI_NOT_READY) {
      // Fastboot owns the USB controller, so scan after a restart.
      Status = RestartToScan (M);
      AppOnFailed (
        &mApp, Status == EFI_SUCCESS ? gStrings->RestartingToScan
                                     : gStrings->RestartToScanFailed
      );
    } else {
      AppOnFailed (&mApp, gStrings->ModelScanFailed);
    }
    FreePool (Models);
    break;
  }
  case COMMAND_REBOOT: {
    UINT32 Action;
    switch ((REBOOT_TARGET)Command.Arg) {
    case REBOOT_LOADER:
      Action = OPENABL_MENU_ACTION_RESTART_BOOTLOADER;
      break;
    case REBOOT_RECOVERY:
      Action = OPENABL_MENU_ACTION_RECOVERY;
      break;
    case REBOOT_EDL:
      Action = OPENABL_MENU_ACTION_EMERGENCY;
      break;
    case REBOOT_OTHER_SLOT:
      Action = OPENABL_MENU_ACTION_ALTERNATE_SLOT;
      break;
    default:
      Action = OPENABL_MENU_ACTION_START;
    }
    if (Act (M, Action) != EFI_SUCCESS) {
      AppOnFailed (&mApp, gStrings->AblActionFailed);
    }
    break;
  }
  case COMMAND_POWER_OFF:
    if (Act (M, OPENABL_MENU_ACTION_POWER_OFF) != EFI_SUCCESS) {
      AppOnFailed (&mApp, gStrings->PowerOffFailed);
    }
    break;
  case COMMAND_UNINSTALL_CFW: {
    SETTINGS     Want = M->Stored;
    BOOLEAN      Android;
    BOOLEAN      Wipe;
    EFI_STATUS   Status;
    CONST CHAR8 *Args[1];
    CONST CHAR8 *Why;
    CHAR8        Text[192];

    // The GPT first: a failure there leaves everything as it was, where
    // asking for the wipe first could erase Android's data for nothing.
    Status = UninstallRun (gMenuEfiLog);
    if (Status != EFI_SUCCESS) {
      // UninstallRun's status says which step failed.
      Why = Status == EFI_NOT_FOUND          ? gStrings->UninstallNotFound
            : Status == EFI_VOLUME_CORRUPTED ? gStrings->UninstallUntrusted
            : Status == EFI_CRC_ERROR        ? gStrings->UninstallReadBack
            : Status == EFI_OUT_OF_RESOURCES ? gStrings->UninstallError
                                             : gStrings->UninstallWriteFailed;
      Args[0] = MENU_STATUS (Status);
      LangFill (Text, sizeof (Text), Why, Args, 1);
      AppOnUninstallFailed (&mApp, Text);
      break;
    }
    M->Caps.CfwOnInternal = FALSE;
    // With Linux as the boot mode the reboot reason is ignored and Linux
    // is looked for, so recovery needs Android as the boot mode. Set it
    // first: a wipe request left in misc without it would fire whenever
    // Android is next picked. The boot source stays: an SD card may still
    // hold Linux.
    Want.BootMode = BOOT_MODE_ANDROID;
    Android = !Save (M, &Want) && M->Stored.BootMode == BOOT_MODE_ANDROID;
    AppSetSettings (&mApp, &M->Stored);
    AppSetCaps (&mApp, &M->Caps);
    AppSetCfwParts (&mApp, NULL, 0);
    if (!Android) {
      // A restart would look for the Linux that's gone, so stay and say
      // what to do.
      MenuLog ("uninstall: Android couldn't be made the boot mode");
      AppOnUninstalled (&mApp, FALSE, FALSE);
      break;
    }
    // Recovery formats userdata at its new size. Without the message it
    // still offers a factory reset by hand.
    Wipe = UninstallRequestWipe (gMenuEfiLog) == EFI_SUCCESS;
    // The restarting screen stays until the restart, which the firmware's
    // handles for the removed partitions don't outlive.
    AppOnUninstalled (&mApp, TRUE, Wipe);
    M->RestartAt = ClockNowMs () + UNINSTALL_RESTART_MS;
    break;
  }
  case COMMAND_SAVE_EXPERIMENTS: {
#ifdef OABL_DEBUG
    EXPERIMENTS Want = Command.Experiments;
    EXPERIMENTS Now;
    BOOLEAN     Ok = M->Writable && SaveExperiments (Want) == EFI_SUCCESS;

    Now = Experiments ();
    AppSetExperiments (&mApp, &Now);
    if (!Ok || Now.UsbLive != Want.UsbLive || Now.Quiesce != Want.Quiesce ||
        Now.PanelLinesOff != Want.PanelLinesOff) {
      AppOnFailed (&mApp, gStrings->AblSettingFailed);
    }
#endif
    break;
  }
  case COMMAND_LOG: {
    OABL_BUFFER Raw   = {0};
    CHAR8     **Lines = AllocateZeroArray (LOG_LINES, sizeof (*Lines));
    UINTN       Count = 0;

    AblConsoleRead (LOG_MAX, &Raw);
    if (Lines) {
      Count = LogLines (&Raw, Lines);
    }
    BufferFree (&Raw);
    MenuLog ("log: %llu lines", (UINT64)Count);
    AppOnLog (&mApp, (CONST CHAR8 *CONST *)Lines, Count);
    for (UINTN Index = 0; Index < Count; Index++) {
      FreePool (Lines[Index]);
    }
    FreePool (Lines);
    break;
  }
  case COMMAND_DEVICE_INFO: {
    TEXT_LINE *Rows  = AllocateZeroArray (24, sizeof (*Rows));
    UINTN      Count = Rows ? DeviceInfo (M, Rows, 24) : 0;

    MenuLog ("device info: %llu lines", (UINT64)Count);
    for (UINTN Index = 0; Index < Count; Index++) {
      MenuLog ("  %s: %s", Rows[Index].Label, Rows[Index].Value);
    }
    AppOnDeviceInfo (&mApp, Rows, Count);
    FreePool (Rows);
    break;
  }
  }
  return FALSE;
}

// Menu loop

STATIC DISPLAY mDisplay;

STATIC VOID
BlitToPanel (
  IN CONST UINT32 *Px,
  IN UINT32        Stride,
  IN UINT32        X,
  IN UINT32        Y,
  IN UINT32        W,
  IN UINT32        H
)
{
  DisplayBlit (&mDisplay, Px, Stride, X, Y, W, H);
}

STATIC UINT32
NowMs (VOID)
{
  return (UINT32)ClockNowMs ();
}

// Commands waiting to run, oldest first.
typedef struct _PENDING_COMMANDS {
  COMMAND Items[8];
  UINTN   Count;
} PENDING_COMMANDS;

// Queues Command, unless it is COMMAND_NONE or the queue is full.
STATIC VOID
Pend (
  IN OUT PENDING_COMMANDS *Pending,
  IN COMMAND               Command
)
{
  if (Command.Kind != COMMAND_NONE &&
      Pending->Count < sizeof (Pending->Items) / sizeof (*Pending->Items)) {
    Pending->Items[Pending->Count++] = Command;
  }
}

// Takes the oldest command off a non-empty queue.
STATIC COMMAND
NextPending (IN OUT PENDING_COMMANDS *Pending)
{
  COMMAND Command = Pending->Items[0];

  for (UINTN Index = 1; Index < Pending->Count; Index++) {
    Pending->Items[Index - 1] = Pending->Items[Index];
  }
  Pending->Count--;
  return Command;
}

MENU_END
MenuRun (
  IN CONST MENU_HOST *Host,
  IN BOOLEAN          StartModels,
  IN UINT8            CameBack
)
{
  STATIC MENU_STATE M;
  STATIC CHAR8      Version[64];
  DEVICE_INFO       Info;
  INPUT_KEYS        Keys    = InputKeysNew ();
  PENDING_COMMANDS  Pending = {0};
  PANEL             Panel;
  SCHEME            Scheme;
  QCOM_CHIP         Chip;
  UINT16            Rotation;
  UINT16            FirmwareRotation;
  EFI_STATUS        Status;
  UINTN             PanelW;
  UINTN             PanelH;

  Status = MenuNew (&M, Host, StartModels, CameBack, &Info);
  if (EFI_ERROR (Status)) {
    MenuLog ("reading the ABL's state failed: %s", MENU_STATUS (Status));
    return (MENU_END){MENU_END_FAILED, Status};
  }
  if (M.CameBack == MENU_START_RESULT_NO_USB) {
    // USB host didn't come up (no drive, or XBL kept the core). Restart, so
    // the next boot brings it up before fastboot.
    MenuLog (
      "Start Linux: USB host didn't come up in this boot; restarting for it"
    );
    // Returns only if the restart failed, and then the menu runs.
    Act (&M, OPENABL_MENU_ACTION_START);
  }
  if (!StartModels) {
    LearnModelRotation (&M);
  }
  Status = DisplayOpen (&mDisplay);
  if (EFI_ERROR (Status)) {
    MenuHostLog (&M.Host, "openabl-menu: no graphics output");
    return (MENU_END){MENU_END_FAILED, Status};
  }
  DisplaySize (&mDisplay, &PanelW, &PanelH);
  Panel = (PANEL){.W       = (UINT32)PanelW,
                  .H       = (UINT32)PanelH,
                  .Present = BlitToPanel,
                  .Now     = NowMs};
  if (!ViewInit (&Panel, gOablAssetFont, gOablAssetFontSize)) {
    MenuHostLog (&M.Host, "openabl-menu: the font didn't load");
    ViewDone ();
    return (MENU_END){MENU_END_FAILED, EFI_LOAD_ERROR};
  }
  Scheme     = QcomChip (&Chip) ? SCHEME_HANDHELD : SCHEME_KEYBOARD;
  M.DisplayW = PanelW;
  M.DisplayH = PanelH;
  // The header shows the ABL's version.
  AsciiStrCopy (
    Version, sizeof (Version), M.AblVersion[0] ? M.AblVersion : MENU_VERSION
  );
  AppInit (&mApp, &M.Stored, &M.Caps, &Info, Scheme, Version);
  {
    CONST GPT_CFW_PART *Parts;
    UINTN               Count = UninstallCfwParts (&Parts);
    AppSetCfwParts (&mApp, Parts, Count);
  }
#ifdef OABL_STANDARD_BOOT
  FillEntries (&M, &mApp);
#endif
#ifdef OABL_DEBUG
  mApp.Debug = TRUE;
#endif
  AppSetPhysicalSize (&mApp, (UINT32)PanelW, (UINT32)PanelH);
  if (QcomFirmwarePanelRotation (PanelW, PanelH, &FirmwareRotation)) {
    AppSetFirmwareRotation (&mApp, FirmwareRotation);
    if (FirmwareRotation != 0) {
      MenuLog ("XBL turns the panel by %u\xc2\xb0", FirmwareRotation);
    }
  }
#ifdef OABL_DEBUG
  {
    EXPERIMENTS Switches = Experiments ();
    AppSetExperiments (&mApp, &Switches);
  }
#endif
  AppStart (&mApp, ClockNowMs (), TRUE);
  {
    BOOLEAN      Ok;
    BOOT_ERROR   Error;
    CONST CHAR8 *Detail;
    if (CameBackShown (&M, &Ok, &Error, &Detail)) {
      AppOnBootResult (&mApp, Ok, Error, Detail);
    }
  }
  if (StartModels) {
    Pend (&Pending, AppOpenModels (&mApp));
  }
  Rotation = AppDesiredRotation (&mApp);
  MenuLog (
    "display %llux%llu, rotation %u", (UINT64)PanelW, (UINT64)PanelH, Rotation
  );

  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  // Ignore a key still held from opening the menu.
  InputKeysHeldAtStart (&Keys, ClockNowMs ());
  ViewRotate (Rotation);
  mApp.Dirty = TRUE;

  for (;;) {
    UINT64       Now = ClockNowMs ();
    UINT64       Until;
    INPUT_EVENTS Events = {0};
    UINT16       Want;

    InputKeysPoll (&Keys, Now, &Events);
    // Once an uninstall waits to restart into recovery, keys and timers do
    // nothing, so its settings can't be changed back before the wipe.
    for (UINTN Index = 0; Index < Events.Count && !M.RestartAt; Index++) {
      Pend (&Pending, AppInput (&mApp, Events.Items[Index]));
    }
    VecFree (&Events);
    {
      COMMAND Tick = AppTick (&mApp, Now);
      if (!M.RestartAt) {
        Pend (&Pending, Tick);
      }
    }
    Want = AppDesiredRotation (&mApp);
    if (Want != Rotation) {
      MenuLog ("rotation -> %u", Want);
      Rotation = Want;
      ViewRotate (Rotation);
      mApp.Dirty = TRUE;
    }
    if (mApp.Dirty) {
      ViewSync (&mApp);
    }
    ViewRun ();
    if (M.RestartAt && ClockNowMs () >= M.RestartAt) {
      MenuLog ("restarting into recovery");
      MenuFlush ();
      Act (&M, OPENABL_MENU_ACTION_RECOVERY);
      // The ABL reads misc's boot-recovery on any restart.
      gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
    }
    // Commands run after the frame is on screen.
    if (Pending.Count) {
      ViewSettle ();
      if (RunCommand (&M, NextPending (&Pending))) {
        ViewDone ();
        return (MENU_END){MENU_END_START_HERE, EFI_ABORTED};
      }
      continue;
    }
    // Give idle time to fastboot. The ABL serves one USB event per poll,
    // and a download needs polls back to back.
    Until = ClockNowMs () + 8;
    for (;;) {
      Status = MenuHostPoll (&M.Host);
      if (Status != EFI_SUCCESS) {
        MenuLog ("the ABL asked to leave (%s)", MENU_STATUS (Status));
        ViewDone ();
        return (MENU_END){MENU_END_LEFT, Status};
      }
      if (ClockNowMs () >= Until) {
        break;
      }
    }
  }
}
