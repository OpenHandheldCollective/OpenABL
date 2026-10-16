/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  App - the menu's state machine (Ui/Ui.h).
*/

#include "Base/Memory.h"
#include "Base/Text.h"
#include "Core/Settings.h"
#include "Ui/Lang.h"
#include "Ui/Ui.h"

// How long a toast shows, in milliseconds.
#define TOAST_MS 2500

STATIC COMMAND
None (VOID)
{
  return (COMMAND){.Kind = COMMAND_NONE};
}

STATIC COMMAND
Command (
  IN COMMAND_KIND Kind,
  IN INT32        Arg
)
{
  return (COMMAND){.Kind = Kind, .Arg = Arg};
}

UINTN
AppSize (VOID)
{
  return sizeof (APP);
}

UINTN
AppSettingsSize (VOID)
{
  return sizeof (SETTINGS);
}

VOID
AppInit (
  OUT APP                *A,
  IN CONST SETTINGS      *Settings,
  IN CONST SETTINGS_CAPS *Caps,
  IN CONST DEVICE_INFO   *Info,
  IN SCHEME               Scheme,
  IN CONST CHAR8         *Version
)
{
  ZeroMem (A, sizeof (*A));
  A->Settings   = *Settings;
  A->Caps       = *Caps;
  A->Info       = *Info;
  A->Scheme     = Scheme;
  A->Version    = Version;
  A->Screen     = SCREEN_MENU;
  A->FocusItem  = ITEM_START;
  A->LinesTitle = "";
  A->LinesFrom  = ITEM_DEVICE_INFO;
  A->Booting    = -1;
  A->Dirty      = TRUE;
  LangSet (Settings->Language);
}

VOID
AppSetSettings (
  IN OUT APP        *A,
  IN CONST SETTINGS *Settings
)
{
  A->Settings = *Settings;
  A->Dirty    = TRUE;
  LangSet (Settings->Language);
}

VOID
AppSetCaps (
  IN OUT APP             *A,
  IN CONST SETTINGS_CAPS *Caps
)
{
  A->Caps  = *Caps;
  A->Dirty = TRUE;
}

VOID
AppSetCfwParts (
  IN OUT APP            *A,
  IN CONST GPT_CFW_PART *Parts OPTIONAL,
  IN UINTN               Count
)
{
  A->CfwPartCount = Parts && Count < MAX_CFW_PARTS ? Count
                    : Parts                        ? MAX_CFW_PARTS
                                                   : 0;
  for (UINTN Index = 0; Index < A->CfwPartCount; Index++) {
    A->CfwParts[Index] = Parts[Index];
  }
  A->Dirty = TRUE;
}

VOID
AppSetPhysicalSize (
  IN OUT APP *A,
  IN UINT32   W,
  IN UINT32   H
)
{
  A->PhysicalW = W;
  A->PhysicalH = H;
  A->Dirty     = TRUE;
}

VOID
AppSetFirmwareRotation (
  IN OUT APP *A,
  IN UINT16   Degrees
)
{
  A->FirmwareRotation = Degrees % 360;
  A->Dirty            = TRUE;
}

VOID
AppSetExperiments (
  IN OUT APP           *A,
  IN CONST EXPERIMENTS *Experiments
)
{
  A->Experiments = *Experiments;
  A->Dirty       = TRUE;
}

VOID
AppSetEntries (
  IN OUT APP          *A,
  IN CONST ENTRY_VIEW *Entries,
  IN UINTN             Count
)
{
  if (Count > MAX_ENTRIES) {
    Count = MAX_ENTRIES;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    A->Entries[Index] = Entries[Index];
  }
  A->EntryCount = Count;
  A->EntryList  = (LIST_STATE){0};
  A->Dirty      = TRUE;
}

UINT16
AppDesiredRotation (IN CONST APP *A)
{
  CONST SETTINGS *Settings = &A->Settings;
  UINT16          Panel;

  if (Settings->Rotation != ROTATION_AUTO) {
    return SettingsEffectiveRotation (Settings);
  }
  if (Settings->ModelRotation == ROTATION_NONE && A->PhysicalH > A->PhysicalW) {
    Panel = 270;
  } else {
    Panel = SettingsEffectiveRotation (Settings);
  }
  return (UINT16)((Panel + A->FirmwareRotation) % 360);
}

STATIC VOID
ShowToast (
  IN OUT APP     *A,
  IN CONST CHAR8 *Message
)
{
  AsciiStrCopy (A->Toast, sizeof (A->Toast), Message);
  A->ToastUntil = A->Now + TOAST_MS;
  A->Dirty      = TRUE;
}

// Starts the boot from the menu. Shows the error screen instead when
// Android is chosen but can't be booted here.
STATIC COMMAND
BeginBoot (IN OUT APP *A)
{
  if (A->Settings.BootMode == BOOT_MODE_ANDROID && !A->Caps.Android) {
    A->Screen = SCREEN_ERROR;
    A->Error  = BOOT_ERROR_ANDROID;
    return None ();
  }
  A->Screen  = SCREEN_BOOTING;
  A->Booting = -1;
  A->Dirty   = TRUE;
  return Command (COMMAND_BOOT, 0);
}

COMMAND
AppStart (
  IN OUT APP *A,
  IN UINT64   Now,
  IN BOOLEAN  ForceMenu
)
{
  A->Now   = Now;
  A->Dirty = TRUE;
  if (ForceMenu) {
    return None ();
  }
  if (A->Settings.MenuTimeoutS == 0) {
    return BeginBoot (A);
  }
  A->Countdown      = TRUE;
  A->CountdownUntil = Now + (UINT64)A->Settings.MenuTimeoutS * 1000;
  return None ();
}

COMMAND
AppOpenModels (IN OUT APP *A)
{
  A->FocusItem = ITEM_MODEL;
  A->Screen    = SCREEN_SCANNING;
  A->Dirty     = TRUE;
  return Command (COMMAND_SCAN_MODELS, 0);
}

COMMAND
AppTick (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  A->Now = Now;
  if (A->ToastUntil && Now >= A->ToastUntil) {
    A->ToastUntil = 0;
    A->Dirty      = TRUE;
  }
  if (A->Countdown) {
    if (Now >= A->CountdownUntil) {
      A->Countdown = FALSE;
      return BeginBoot (A);
    }
    UINT64 Seconds = (A->CountdownUntil - Now + 999) / 1000;
    if (A->ShownCountdown != Seconds) {
      A->ShownCountdown = Seconds;
      A->Dirty          = TRUE;
    }
  }
  return None ();
}

BOOLEAN
AppCountdownSeconds (
  IN CONST APP *A,
  OUT UINT64   *Seconds
)
{
  if (!A->Countdown) {
    return FALSE;
  }
  UINT64 Left = A->CountdownUntil > A->Now ? A->CountdownUntil - A->Now : 0;
  *Seconds    = (Left + 999) / 1000;
  return TRUE;
}

// Results from the platform

VOID
AppOnBootResult (
  IN OUT APP     *A,
  IN BOOLEAN      Ok,
  IN BOOT_ERROR   Error,
  IN CONST CHAR8 *Detail OPTIONAL
)
{
  A->Dirty = TRUE;
  if (Ok) {
    A->Screen = SCREEN_MENU;
    ShowToast (A, gStrings->ToastTargetExited);
    return;
  }
  A->Screen = SCREEN_ERROR;
  A->Error  = Error;
  AsciiStrCopy (A->ErrorDetail, sizeof (A->ErrorDetail), Detail ? Detail : "");
}

VOID
AppOnUninstalled (
  IN OUT APP *A,
  IN BOOLEAN  Restarting,
  IN BOOLEAN  Wiped
)
{
  A->Dirty      = TRUE;
  A->ToastUntil = 0;
  A->Wiped      = Wiped;
  if (Restarting) {
    A->Screen = SCREEN_RESTARTING;
    return;
  }
  A->Screen         = SCREEN_ERROR;
  A->Error          = BOOT_ERROR_WIPE_BY_HAND;
  A->ErrorDetail[0] = 0;
}

// Opens the device info or the log (already in A->Lines) at line Focus.
// Back returns to menu item From.
STATIC VOID
ShowLines (
  IN OUT APP     *A,
  IN CONST CHAR8 *Title,
  IN ITEM         From,
  IN UINTN        Focus
)
{
  A->LinesTitle = Title;
  A->LinesFrom  = From;
  A->InfoList   = (LIST_STATE){.Focus = Focus, .Scroll = Focus};
  A->Screen     = SCREEN_LINES;
  A->Dirty      = TRUE;
}

VOID
AppOnDeviceInfo (
  IN OUT APP         *A,
  IN CONST TEXT_LINE *Lines,
  IN UINTN            Count
)
{
  if (Count > MAX_LINES) {
    Count = MAX_LINES;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    A->Lines[Index] = Lines[Index];
  }
  A->LineCount = Count;
  ShowLines (A, gStrings->DeviceInfo, ITEM_DEVICE_INFO, 0);
}

VOID
AppOnLog (
  IN OUT APP            *A,
  IN CONST CHAR8 *CONST *Lines,
  IN UINTN               Count
)
{
  // Keep the newest when there are more than fit.
  UINTN Skip = Count > MAX_LINES ? Count - MAX_LINES : 0;

  Count -= Skip;
  for (UINTN Index = 0; Index < Count; Index++) {
    A->Lines[Index].Label = "";
    AsciiStrCopy (
      A->Lines[Index].Value, sizeof (A->Lines[Index].Value), Lines[Skip + Index]
    );
  }
  A->LineCount = Count;
  ShowLines (A, gStrings->Log, ITEM_LOG, Count ? Count - 1 : 0);
}

VOID
AppOnModels (
  IN OUT APP     *A,
  IN CONST MODEL *Models,
  IN UINTN        Count
)
{
  if (Count > MAX_MODELS) {
    Count = MAX_MODELS;
  }
  A->ModelList = (LIST_STATE){0};
  for (UINTN Index = 0; Index < Count; Index++) {
    A->Models[Index] = Models[Index];
    // Row 0 is "Firmware default", then the models.
    if (A->Settings.Model[0] && !A->ModelList.Focus &&
        AsciiStrEq (Models[Index].Name, A->Settings.Model)) {
      A->ModelList.Focus = Index + 1;
    }
  }
  A->ModelCount = Count;
  A->Screen     = SCREEN_MODELS;
  A->Dirty      = TRUE;
}

VOID
AppOnUninstallFailed (
  IN OUT APP     *A,
  IN CONST CHAR8 *Why
)
{
  A->Dirty      = TRUE;
  A->ToastUntil = 0;
  A->Screen     = SCREEN_ERROR;
  A->Error      = BOOT_ERROR_UNINSTALL;
  AsciiStrCopy (A->ErrorDetail, sizeof (A->ErrorDetail), Why);
}

VOID
AppOnFailed (
  IN OUT APP     *A,
  IN CONST CHAR8 *Message
)
{
  if (A->Screen == SCREEN_BOOTING || A->Screen == SCREEN_SCANNING ||
      A->Screen == SCREEN_CONFIRM || A->Screen == SCREEN_UNINSTALLING) {
    A->Screen = SCREEN_MENU;
  }
  ShowToast (A, Message);
}

VOID
AppNotify (
  IN OUT APP     *A,
  IN CONST CHAR8 *Message
)
{
  ShowToast (A, Message);
}

// What the view shows

CONST CHAR8 *
AppBootTitle (IN CONST APP *A)
{
  UINTN Index = A->Booting >= 0 ? (UINTN)A->Booting : 0;
  return Index < A->EntryCount ? A->Entries[Index].Title : NULL;
}

VOID
AppBootDetail (
  IN CONST APP *A,
  OUT CHAR8    *Out,
  IN UINTN      Cap
)
{
  UINTN        Index  = A->Booting >= 0 ? (UINTN)A->Booting : 0;
  CONST CHAR8 *Source = "USB";

  if (Index < A->EntryCount) {
    AsciiStrCopy (Out, Cap, A->Entries[Index].Detail);
    return;
  }
  switch (A->Settings.BootSource) {
  case BOOT_SOURCE_AUTO:
    Source = gStrings->SrcFirstDrive;
    break;
  case BOOT_SOURCE_INTERNAL:
    Source = gStrings->SrcInternal;
    break;
  case BOOT_SOURCE_SD:
    Source = gStrings->SrcSd;
    break;
  case BOOT_SOURCE_USB:
    break;
  }
  LangFill (Out, Cap, gStrings->LookingOn, &Source, 1);
}

UINTN
AppRows (
  IN CONST APP *A,
  OUT ROW      *Rows
)
{
  CONST SETTINGS      *Settings = &A->Settings;
  CONST SETTINGS_CAPS *Caps     = &A->Caps;
  UINTN                Count    = 0;

#define ADD_HEADER(Title) (Rows[Count++] = (ROW){.Header = (Title)})
#define ADD_ITEM(Id)      (Rows[Count++] = (ROW){.Item = (Id)})
  ADD_HEADER (gStrings->SectionBoot);
  ADD_ITEM (ITEM_START);
  if (A->EntryCount) {
    ADD_ITEM (ITEM_ENTRIES);
  }
  ADD_ITEM (ITEM_RESTART_LOADER);
  ADD_ITEM (ITEM_RECOVERY);
  ADD_ITEM (ITEM_EDL);
  if (Caps->AlternateSlot) {
    ADD_ITEM (ITEM_OTHER_SLOT);
  }
  ADD_HEADER (gStrings->SectionSettings);
  // First, so it is easy to find in a language one can't read.
  ADD_ITEM (ITEM_LANGUAGE);
  // Without Android support the row stays visible, read-only, so it's
  // clear where the choice went.
  ADD_ITEM (ITEM_BOOT_MODE);
  ADD_ITEM (ITEM_ABL_MODE);
  ADD_ITEM (ITEM_ALT_VOLUME_UP);
  if (SettingsBootSourceVisible (Settings, Caps)) {
    ADD_ITEM (ITEM_BOOT_SOURCE);
  }
  ADD_ITEM (ITEM_CHARGING);
  ADD_ITEM (ITEM_ROTATION);
  ADD_ITEM (ITEM_CUSTOM_LOGO);
  if (!Caps->Embedded) {
    ADD_ITEM (ITEM_TIMEOUT);
  }
  if (SettingsModelVisible (Settings)) {
    ADD_ITEM (ITEM_MODEL);
  }
  // Experiments, in debug builds only.
  if (A->Debug && Caps->Embedded) {
    ADD_HEADER (gStrings->SectionDebug);
    if (Caps->UsbHost) {
      ADD_ITEM (ITEM_USB_LIVE);
    }
    ADD_ITEM (ITEM_QUIESCE);
    ADD_ITEM (ITEM_PANEL_LINES);
  }
  ADD_HEADER (gStrings->SectionSystem);
  if (Caps->Log) {
    ADD_ITEM (ITEM_LOG);
  }
  if (Caps->DeviceInfo) {
    ADD_ITEM (ITEM_DEVICE_INFO);
  }
  if (Caps->CfwOnInternal) {
    ADD_ITEM (ITEM_UNINSTALL);
  }
  ADD_ITEM (ITEM_POWER_OFF);
#undef ADD_HEADER
#undef ADD_ITEM
  return Count;
}

// Fills Items (MAX_ROWS long) with the menu items, without headers.
STATIC UINTN
MenuItems (
  IN CONST APP *A,
  OUT ITEM     *Items
)
{
  ROW   Rows[MAX_ROWS];
  UINTN RowCount = AppRows (A, Rows);
  UINTN Count    = 0;

  for (UINTN Index = 0; Index < RowCount; Index++) {
    if (!Rows[Index].Header) {
      Items[Count++] = Rows[Index].Item;
    }
  }
  return Count;
}

UINTN
AppFocusedRow (
  IN CONST APP *A,
  IN CONST ROW *Rows,
  IN UINTN      Count
)
{
  for (UINTN Index = 0; Index < Count; Index++) {
    if (!Rows[Index].Header && Rows[Index].Item == A->FocusItem) {
      return Index;
    }
  }
  return 1;
}

// What volume up does at boot, as shown in the menu.
STATIC CONST CHAR8 *
AltVolumeLabel (IN CONST APP *A)
{
  if (A->Settings.AltVolumeUp) {
    return gStrings->VolLinuxExternal;
  }
  return A->Settings.BootMode == BOOT_MODE_ANDROID ? gStrings->VolRecovery
                                                   : "Android";
}

// Formats the rotation value: the degrees, or Auto and what it resolves to.
STATIC VOID
RotationLabel (
  IN CONST APP *A,
  OUT CHAR8    *Out,
  IN UINTN      Cap
)
{
  CHAR8 Degrees[8];

  if (A->Settings.Rotation == ROTATION_AUTO) {
    CONST CHAR8 *Arg = Degrees;
    AsciiStrFromU64 (Degrees, sizeof (Degrees), AppDesiredRotation (A));
    LangFill (Out, Cap, gStrings->RotationAuto, &Arg, 1);
  } else {
    AsciiStrFromU64 (Out, Cap, A->Settings.Rotation);
    AsciiStrAppend (Out, Cap, "°");
  }
}

VOID
AppItemView (
  IN CONST APP  *A,
  IN ITEM        Item,
  OUT ITEM_VIEW *View
)
{
  CONST SETTINGS *Settings = &A->Settings;
  CHAR8          *Value    = View->Value;
  UINTN           Cap      = sizeof (View->Value);

  *View = (ITEM_VIEW){.Tone = TONE_NORMAL, .Trailing = TRAILING_CYCLE};
  switch (Item) {
  case ITEM_START:
    View->Icon     = ICON_PLAY;
    View->Tone     = TONE_PRIMARY;
    View->Trailing = TRAILING_NONE;
    if (Settings->BootMode == BOOT_MODE_LINUX && A->EntryCount) {
      View->Label = gStrings->Start;
      AsciiStrCopy (Value, Cap, A->Entries[0].Title);
    } else {
      View->Label = Settings->BootMode == BOOT_MODE_ANDROID
                      ? gStrings->StartAndroid
                      : gStrings->StartLinux;
    }
    break;
  case ITEM_ENTRIES:
    View->Icon  = ICON_STACK;
    View->Label = gStrings->BootEntries;
    AsciiStrFromU64 (Value, Cap, A->EntryCount);
    View->Trailing = TRAILING_NAVIGATE;
    break;
  case ITEM_RESTART_LOADER:
    View->Icon     = ICON_REFRESH;
    View->Label    = gStrings->RestartBootloader;
    View->Trailing = TRAILING_NONE;
    break;
  case ITEM_RECOVERY:
    View->Icon     = ICON_LIFEBUOY;
    View->Label    = gStrings->RecoveryMode;
    View->Trailing = TRAILING_NONE;
    break;
  case ITEM_EDL:
    View->Icon     = ICON_CPU;
    View->Label    = gStrings->Edl;
    View->Trailing = TRAILING_NAVIGATE;
    break;
  case ITEM_OTHER_SLOT:
    View->Icon     = ICON_SWITCH;
    View->Label    = gStrings->OtherSlot;
    View->Tone     = TONE_DANGER;
    View->Trailing = TRAILING_NONE;
    break;
  case ITEM_LANGUAGE:
    View->Icon  = ICON_SWITCH;
    View->Label = gStrings->Language;
    AsciiStrCopy (Value, Cap, LangName (Settings->Language));
    break;
  case ITEM_BOOT_MODE:
    View->Icon  = ICON_SWITCH;
    View->Label = gStrings->BootMode;
    AsciiStrCopy (
      Value, Cap, Settings->BootMode == BOOT_MODE_ANDROID ? "Android" : "Linux"
    );
    View->Trailing = A->Caps.Android ? TRAILING_CYCLE : TRAILING_NONE;
    break;
  case ITEM_ABL_MODE:
    View->Icon  = ICON_TERMINAL;
    View->Label = gStrings->AblMode;
    AsciiStrCopy (
      Value, Cap,
      Settings->AblMode == ABL_MODE_VERBOSE ? gStrings->Verbose
                                            : gStrings->Silent
    );
    break;
  case ITEM_ALT_VOLUME_UP:
    View->Icon  = ICON_VOLUME;
    View->Label = gStrings->VolumeUp;
    AsciiStrCopy (Value, Cap, AltVolumeLabel (A));
    break;
  case ITEM_BOOT_SOURCE: {
    STATIC CONST CHAR8 *CONST Usb       = "USB";
    CONST CHAR8 *CONST        Sources[] = {gStrings->Auto, gStrings->Internal,
                                           gStrings->SdCard, Usb};
    View->Icon                          = ICON_SD_CARD;
    View->Label                         = gStrings->BootSource;
    AsciiStrCopy (Value, Cap, Sources[Settings->BootSource]);
    break;
  }
  case ITEM_CHARGING:
    View->Icon  = ICON_BATTERY;
    View->Label = gStrings->Charging;
    AsciiStrCopy (
      Value, Cap, Settings->OffmodeCharging ? gStrings->On : gStrings->Off
    );
    break;
  case ITEM_ROTATION:
    View->Icon  = ICON_ROTATE;
    View->Label = gStrings->Rotation;
    RotationLabel (A, Value, Cap);
    break;
  case ITEM_CUSTOM_LOGO:
    View->Icon  = ICON_SWITCH;
    View->Label = gStrings->CustomLogo;
    AsciiStrCopy (
      Value, Cap, Settings->CustomLogo ? gStrings->On : gStrings->Off
    );
    break;
  case ITEM_TIMEOUT:
    View->Icon  = ICON_CLOCK;
    View->Label = gStrings->Timeout;
    if (Settings->MenuTimeoutS == 0) {
      AsciiStrCopy (Value, Cap, gStrings->Off);
    } else {
      CHAR8        Seconds[4];
      CONST CHAR8 *Arg = Seconds;
      AsciiStrFromU64 (Seconds, sizeof (Seconds), Settings->MenuTimeoutS);
      LangFill (Value, Cap, gStrings->Seconds, &Arg, 1);
    }
    break;
  case ITEM_MODEL:
    View->Icon  = ICON_GAMEPAD;
    View->Label = gStrings->DeviceModel;
    AsciiStrCopy (
      Value, Cap, Settings->Model[0] ? Settings->Model : gStrings->NotSet
    );
    View->Trailing = TRAILING_NAVIGATE;
    break;
  case ITEM_UNINSTALL:
    View->Icon     = ICON_TRASH;
    View->Label    = gStrings->UninstallCfw;
    View->Tone     = TONE_DANGER;
    View->Trailing = TRAILING_NAVIGATE;
    break;
  case ITEM_DEVICE_INFO:
    View->Icon     = ICON_INFO;
    View->Label    = gStrings->DeviceInfo;
    View->Trailing = TRAILING_NAVIGATE;
    break;
  case ITEM_LOG:
    View->Icon     = ICON_TERMINAL;
    View->Label    = gStrings->Log;
    View->Trailing = TRAILING_NAVIGATE;
    break;
  case ITEM_USB_LIVE:
  case ITEM_QUIESCE:
  case ITEM_PANEL_LINES: {
    BOOLEAN On  = Item == ITEM_USB_LIVE  ? A->Experiments.UsbLive
                  : Item == ITEM_QUIESCE ? A->Experiments.Quiesce
                                         : A->Experiments.PanelLinesOff;
    View->Icon  = ICON_SWITCH;
    View->Label = Item == ITEM_USB_LIVE  ? gStrings->DbgUsbLive
                  : Item == ITEM_QUIESCE ? gStrings->DbgQuiesce
                                         : gStrings->DbgPanelLines;
    AsciiStrCopy (Value, Cap, On ? gStrings->On : gStrings->Off);
    break;
  }
  case ITEM_POWER_OFF:
    View->Icon     = ICON_POWER;
    View->Label    = gStrings->PowerOff;
    View->Trailing = TRAILING_NONE;
    break;
  }
}

// Input

// What Select does on a menu item.
STATIC COMMAND
Activate (
  IN OUT APP *A,
  IN ITEM     Item
)
{
  SETTINGS    *Settings    = &A->Settings;
  EXPERIMENTS *Experiments = &A->Experiments;

  switch (Item) {
  case ITEM_START:
    return BeginBoot (A);
  case ITEM_ENTRIES:
    A->Screen    = SCREEN_ENTRIES;
    A->EntryList = (LIST_STATE){0};
    return None ();
  case ITEM_RESTART_LOADER:
    return Command (COMMAND_REBOOT, REBOOT_LOADER);
  case ITEM_RECOVERY:
    return Command (COMMAND_REBOOT, REBOOT_RECOVERY);
  case ITEM_OTHER_SLOT:
    return Command (COMMAND_REBOOT, REBOOT_OTHER_SLOT);
  case ITEM_EDL:
    A->Screen     = SCREEN_CONFIRM;
    A->Confirm    = CONFIRM_EDL;
    A->ConfirmYes = FALSE;
    return None ();
  case ITEM_LANGUAGE:
    SettingsNextLanguage (Settings);
    LangSet (Settings->Language);
    return Command (COMMAND_SAVE_SETTINGS, 0);
  case ITEM_BOOT_MODE:
    if (!A->Caps.Android) {
      ShowToast (A, gStrings->ToastAndroidInAbl);
      return None ();
    }
    SettingsToggleBootMode (Settings, &A->Caps);
    return Command (COMMAND_SAVE_SETTINGS, 0);
  case ITEM_ABL_MODE:
    Settings->AblMode =
      Settings->AblMode == ABL_MODE_SILENT ? ABL_MODE_VERBOSE : ABL_MODE_SILENT;
    return Command (COMMAND_SAVE_SETTINGS, 0);
  case ITEM_ALT_VOLUME_UP:
    Settings->AltVolumeUp = !Settings->AltVolumeUp;
    return Command (COMMAND_SAVE_SETTINGS, 0);
  case ITEM_BOOT_SOURCE:
    SettingsNextBootSource (Settings, &A->Caps);
    return Command (COMMAND_SAVE_SETTINGS, 0);
  case ITEM_CHARGING:
    Settings->OffmodeCharging = !Settings->OffmodeCharging;
    return Command (COMMAND_SAVE_SETTINGS, 0);
  case ITEM_ROTATION:
    SettingsNextRotation (Settings);
    return Command (COMMAND_SAVE_SETTINGS, 0);
  case ITEM_CUSTOM_LOGO:
    Settings->CustomLogo = !Settings->CustomLogo;
    return Command (COMMAND_SAVE_SETTINGS, 0);
  case ITEM_TIMEOUT:
    SettingsNextTimeout (Settings);
    return Command (COMMAND_SAVE_SETTINGS, 0);
  case ITEM_MODEL:
    A->Screen = SCREEN_SCANNING;
    return Command (COMMAND_SCAN_MODELS, 0);
  case ITEM_UNINSTALL:
    A->Screen     = SCREEN_CONFIRM;
    A->Confirm    = CONFIRM_UNINSTALL;
    A->ConfirmYes = FALSE;
    return None ();
  case ITEM_DEVICE_INFO:
    return Command (COMMAND_DEVICE_INFO, 0);
  case ITEM_LOG:
    return Command (COMMAND_LOG, 0);
  case ITEM_USB_LIVE:
  case ITEM_QUIESCE:
  case ITEM_PANEL_LINES: {
    COMMAND Save = Command (COMMAND_SAVE_EXPERIMENTS, 0);
    if (Item == ITEM_USB_LIVE) {
      Experiments->UsbLive = !Experiments->UsbLive;
    } else if (Item == ITEM_QUIESCE) {
      Experiments->Quiesce = !Experiments->Quiesce;
    } else {
      Experiments->PanelLinesOff = !Experiments->PanelLinesOff;
    }
    Save.Experiments = *Experiments;
    return Save;
  }
  case ITEM_POWER_OFF:
    return Command (COMMAND_POWER_OFF, 0);
  }
  return None ();
}

// Up and down move the focus through the items, wrapping around.
STATIC COMMAND
MenuInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  ITEM  Items[MAX_ROWS];
  UINTN Count   = MenuItems (A, Items);
  UINTN Focused = 0;

  for (UINTN Index = 0; Index < Count; Index++) {
    if (Items[Index] == A->FocusItem) {
      Focused = Index;
      break;
    }
  }
  switch (Input) {
  case INPUT_UP:
    A->FocusItem = Items[(Focused + Count - 1) % Count];
    return None ();
  case INPUT_DOWN:
    A->FocusItem = Items[(Focused + 1) % Count];
    return None ();
  case INPUT_BACK:
    return None ();
  case INPUT_SELECT:
    return Activate (A, Items[Focused]);
  }
  return None ();
}

// Up and down move through the lines, scrolling the list. Select and Back
// go back to the menu.
STATIC COMMAND
InfoInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  UINTN Count = A->LineCount ? A->LineCount : 1;
  UINTN Focus = A->InfoList.Focus;

  switch (Input) {
  case INPUT_UP:
    A->InfoList.Focus = (Focus + Count - 1) % Count;
    break;
  case INPUT_DOWN:
    A->InfoList.Focus = (Focus + 1) % Count;
    break;
  case INPUT_BACK:
  case INPUT_SELECT:
    A->Screen    = SCREEN_MENU;
    A->FocusItem = A->LinesFrom;
    break;
  }
  return None ();
}

STATIC VOID
LeaveModels (IN OUT APP *A)
{
  A->Screen    = SCREEN_MENU;
  A->FocusItem = ITEM_MODEL;
}

STATIC COMMAND
ModelsInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  // Rows: "Firmware default", each model, "Back".
  UINTN Count = A->ModelCount + 2;
  UINTN Focus = A->ModelList.Focus;

  switch (Input) {
  case INPUT_UP:
    A->ModelList.Focus = (Focus + Count - 1) % Count;
    break;
  case INPUT_DOWN:
    A->ModelList.Focus = (Focus + 1) % Count;
    break;
  case INPUT_BACK:
    LeaveModels (A);
    break;
  case INPUT_SELECT:
    if (Focus == 0) {
      // No model, so the OS gets the firmware's devicetree untouched.
      A->Settings.Model[0]      = 0;
      A->Settings.ModelRotation = ROTATION_NONE;
      LeaveModels (A);
      ShowToast (A, gStrings->ToastFirmwareDt);
      return Command (COMMAND_SAVE_SETTINGS, 0);
    }
    if (Focus <= A->ModelCount) {
      CONST MODEL *Model = &A->Models[Focus - 1];
      CONST CHAR8 *Arg   = Model->Name;
      CHAR8        Message[160];

      AsciiStrCopy (A->Settings.Model, sizeof (A->Settings.Model), Model->Name);
      A->Settings.ModelRotation = Model->Rotation;
      LeaveModels (A);
      LangFill (Message, sizeof (Message), gStrings->ToastModelSet, &Arg, 1);
      ShowToast (A, Message);
      return Command (COMMAND_SAVE_SETTINGS, 0);
    }
    LeaveModels (A);
    break;
  }
  return None ();
}

STATIC VOID
LeaveEntries (IN OUT APP *A)
{
  A->Screen    = SCREEN_MENU;
  A->FocusItem = ITEM_ENTRIES;
}

STATIC COMMAND
EntriesInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  // Rows: each entry, then "Back".
  UINTN Count = A->EntryCount + 1;
  UINTN Focus = A->EntryList.Focus;

  switch (Input) {
  case INPUT_UP:
    A->EntryList.Focus = (Focus + Count - 1) % Count;
    break;
  case INPUT_DOWN:
    A->EntryList.Focus = (Focus + 1) % Count;
    break;
  case INPUT_BACK:
    LeaveEntries (A);
    break;
  case INPUT_SELECT:
    if (Focus < A->EntryCount) {
      A->Screen  = SCREEN_BOOTING;
      A->Booting = (INT32)Focus;
      return Command (COMMAND_BOOT_ENTRY, (INT32)Focus);
    }
    LeaveEntries (A);
    break;
  }
  return None ();
}

// Up and down switch between Cancel and the action; Back cancels. Yes on
// the first uninstall card shows the second, with Cancel picked again.
STATIC COMMAND
ConfirmInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  switch (Input) {
  case INPUT_UP:
  case INPUT_DOWN:
    A->ConfirmYes = !A->ConfirmYes;
    return None ();
  case INPUT_BACK:
    A->Screen = SCREEN_MENU;
    return None ();
  case INPUT_SELECT:
    A->Screen = SCREEN_MENU;
    if (!A->ConfirmYes) {
      return None ();
    }
    if (A->Confirm == CONFIRM_UNINSTALL) {
      A->Screen     = SCREEN_CONFIRM;
      A->Confirm    = CONFIRM_UNINSTALL_FINAL;
      A->ConfirmYes = FALSE;
      return None ();
    }
    if (A->Confirm == CONFIRM_UNINSTALL_FINAL) {
      A->Screen = SCREEN_UNINSTALLING;
      return Command (COMMAND_UNINSTALL_CFW, 0);
    }
    return Command (COMMAND_REBOOT, REBOOT_EDL);
  }
  return None ();
}

COMMAND
AppInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  A->Dirty = TRUE;
  if (A->Countdown) {
    // The first press only stops the countdown.
    A->Countdown = FALSE;
    return None ();
  }
  switch (A->Screen) {
  case SCREEN_MENU:
    return MenuInput (A, Input);
  case SCREEN_MODELS:
    return ModelsInput (A, Input);
  case SCREEN_ENTRIES:
    return EntriesInput (A, Input);
  case SCREEN_CONFIRM:
    return ConfirmInput (A, Input);
  case SCREEN_ERROR:
    if (A->Error == BOOT_ERROR_MODEL_NOT_SET) {
      A->Screen = SCREEN_SCANNING;
      return Command (COMMAND_SCAN_MODELS, 0);
    }
    A->Screen = SCREEN_MENU;
    return None ();
  case SCREEN_LINES:
    return InfoInput (A, Input);
  case SCREEN_SCANNING:
  case SCREEN_BOOTING:
  case SCREEN_UNINSTALLING:
  case SCREEN_RESTARTING:
    return None ();
  }
  return None ();
}

// Keys

BOOLEAN
KeysPress (
  IN OUT KEYS *Keys,
  IN INPUT     Key,
  IN UINT64    Now,
  OUT INPUT   *Out
)
{
  if (Keys->Held && Keys->Key == Key && Now - Keys->Last < KEY_GAP_MS) {
    // A repeat. Only Up and Down act on it, to scroll. Repeating Select or
    // Back would trigger the action again.
    Keys->Last = Now;
    if ((Key == INPUT_UP || Key == INPUT_DOWN) &&
        Now - Keys->Start >= KEY_REPEAT_DELAY_MS &&
        Now - Keys->LastEmit >= KEY_REPEAT_EVERY_MS) {
      Keys->LastEmit = Now;
      *Out           = Key;
      return TRUE;
    }
    return FALSE;
  }
  Keys->Held  = TRUE;
  Keys->Key   = Key;
  Keys->Start = Keys->Last = Keys->LastEmit = Now;
  *Out                                      = Key;
  return TRUE;
}

VOID
KeysIdle (
  IN OUT KEYS *Keys,
  IN UINT64    Now
)
{
  if (Keys->Held && Now - Keys->Last >= KEY_GAP_MS) {
    Keys->Held = FALSE;
  }
}
