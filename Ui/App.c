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
  A->FocusItem  = ITEM_PORTAREOS;
  A->LinesTitle = "";
  A->LinesFrom  = ITEM_PORTAREOS;
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

// Starts Target: PortareOS, Android, or the first of the boot order. Shows
// the error screen instead when Android can't be booted here.
STATIC COMMAND
BeginBoot (
  IN OUT APP *A,
  IN BOOT_TARGET Target
)
{
  if (Target == BOOT_TARGET_ORDER) {
    Target = A->Settings.BootMode == BOOT_MODE_ANDROID ? BOOT_TARGET_ANDROID
                                                       : BOOT_TARGET_PORTAREOS;
  }
  A->Starting = Target;
  A->Dirty    = TRUE;
  if (Target == BOOT_TARGET_ANDROID && !A->Caps.Android) {
    A->Screen = SCREEN_ERROR;
    A->Error  = BOOT_ERROR_ANDROID;
    return None ();
  }
  A->Screen  = SCREEN_BOOTING;
  A->Booting = -1;
  return Command (COMMAND_BOOT, Target);
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
    return BeginBoot (A, BOOT_TARGET_ORDER);
  }
  A->Countdown      = TRUE;
  A->CountdownUntil = Now + (UINT64)A->Settings.MenuTimeoutS * 1000;
  return None ();
}

COMMAND
AppOpenModels (IN OUT APP *A)
{
  A->FocusItem = ITEM_PORTAREOS;
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
      return BeginBoot (A, BOOT_TARGET_ORDER);
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
  ShowLines (A, gStrings->DeviceInfo, ITEM_PORTAREOS, 0);
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
  ShowLines (A, gStrings->Log, ITEM_PORTAREOS, Count ? Count - 1 : 0);
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
AppEntryDetail (
  IN CONST ENTRY_VIEW *Entry,
  OUT CHAR8           *Out,
  IN UINTN             Cap
)
{
  CONST CHAR8 *Kind = gStrings->BootImage;

  switch (Entry->Kind) {
  case ENTRY_KIND_BLS:
    Kind = gStrings->EntryType1;
    break;
  case ENTRY_KIND_UKI:
    Kind = gStrings->EntryUki;
    break;
  case ENTRY_KIND_EXTLINUX:
    Kind = "extlinux.conf";
    break;
  case ENTRY_KIND_DEFAULT_FILE:
    Kind = gStrings->DefaultFile;
    break;
  case ENTRY_KIND_BOOT_IMAGE:
    break;
  }
  Out[0] = 0;
  if (Entry->Version[0]) {
    AsciiStrAppend (Out, Cap, Entry->Version);
    AsciiStrAppend (Out, Cap, " \xc2\xb7 ");
  }
  AsciiStrAppend (Out, Cap, Kind);
  AsciiStrAppend (Out, Cap, " \xc2\xb7 ");
  AsciiStrAppend (Out, Cap, Entry->Drive);
  AsciiStrAppend (Out, Cap, " (");
  AsciiStrAppend (
    Out, Cap,
    Entry->Removable ? gStrings->RemovableStorage : gStrings->InternalStorage
  );
  AsciiStrAppend (Out, Cap, ")");
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
    AppEntryDetail (&A->Entries[Index], Out, Cap);
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
  CONST SETTINGS_CAPS *Caps  = &A->Caps;
  UINTN                Count = 0;

#define ADD_HEADER(Title) (Rows[Count++] = (ROW){.Header = (Title)})
#define ADD_ITEM(Id)      (Rows[Count++] = (ROW){.Item = (Id)})
  ADD_HEADER (gStrings->SectionStart);
  ADD_ITEM (ITEM_PORTAREOS);
  if (Caps->Android) {
    ADD_ITEM (ITEM_ANDROID);
    ADD_ITEM (ITEM_BOOT_ORDER);
  }
  // Uninstalling gives Android the space back, so it needs Android.
  if (Caps->Android && Caps->CfwOnInternal) {
    ADD_HEADER (gStrings->SectionStorage);
    ADD_ITEM (ITEM_UNINSTALL);
  }
  ADD_HEADER (gStrings->SectionSystem);
  ADD_ITEM (ITEM_EDL);
  ADD_ITEM (ITEM_RESTART);
  ADD_ITEM (ITEM_POWER_OFF);
  // Experiments, in debug builds only.
  if (A->Debug && Caps->Embedded) {
    ADD_HEADER (gStrings->SectionDebug);
    if (Caps->UsbHost) {
      ADD_ITEM (ITEM_USB_LIVE);
    }
    ADD_ITEM (ITEM_QUIESCE);
    ADD_ITEM (ITEM_PANEL_LINES);
  }
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

// The help line for a system's row: the first of the boot order starts at
// power-on, the other with Volume Up held.
STATIC CONST CHAR8 *
StartHelp (
  IN CONST APP   *A,
  IN BOOT_TARGET  Target,
  OUT CHAR8      *Out,
  IN UINTN        Cap
)
{
  BOOLEAN      AndroidFirst = A->Settings.BootMode == BOOT_MODE_ANDROID;
  BOOLEAN      First = (Target == BOOT_TARGET_ANDROID) == AndroidFirst;
  CONST CHAR8 *Other = Target == BOOT_TARGET_ANDROID ? gStrings->PortareOs
                                                    : gStrings->Android;

  if (!A->Caps.Android) {
    return NULL;
  }
  if (!First) {
    return gStrings->HelpSecond;
  }
  LangFill (Out, Cap, gStrings->HelpFirst, &Other, 1);
  return Out;
}

VOID
AppItemView (
  IN CONST APP  *A,
  IN ITEM        Item,
  OUT ITEM_VIEW *View
)
{
  // Built help lines live here, so they last as long as the caller's view.
  STATIC CHAR8 Help[2][96];
  CHAR8       *Value = View->Value;
  UINTN        Cap   = sizeof (View->Value);

  *View = (ITEM_VIEW){.Verb = gStrings->VerbSelect};
  switch (Item) {
  case ITEM_PORTAREOS:
    View->Label = gStrings->PortareOs;
    View->Verb  = gStrings->VerbLaunch;
    AsciiStrCopy (
      Value, Cap, A->Caps.CfwOnInternal ? gStrings->Internal : gStrings->SdCard
    );
    View->Help = StartHelp (A, BOOT_TARGET_PORTAREOS, Help[0], sizeof (Help[0]));
    break;
  case ITEM_ANDROID:
    View->Label = gStrings->Android;
    View->Verb  = gStrings->VerbLaunch;
    View->Help  = StartHelp (A, BOOT_TARGET_ANDROID, Help[1], sizeof (Help[1]));
    break;
  case ITEM_BOOT_ORDER:
    View->Label = gStrings->BootOrder;
    View->Verb  = gStrings->VerbChange;
    View->Help  = gStrings->HelpBootOrder;
    AsciiStrCopy (
      Value, Cap,
      A->Settings.BootMode == BOOT_MODE_ANDROID ? gStrings->OrderAndroidFirst
                                                : gStrings->OrderPortareOsFirst
    );
    break;
  case ITEM_UNINSTALL:
    View->Label = gStrings->UninstallPortareOs;
    View->Help  = gStrings->HelpUninstall;
    break;
  case ITEM_EDL:
    View->Label = gStrings->Edl;
    View->Help  = gStrings->HelpEdl;
    break;
  case ITEM_RESTART:
    View->Label = gStrings->Restart;
    break;
  case ITEM_POWER_OFF:
    View->Label = gStrings->ShutDown;
    break;
  case ITEM_USB_LIVE:
  case ITEM_QUIESCE:
  case ITEM_PANEL_LINES: {
    BOOLEAN On  = Item == ITEM_USB_LIVE  ? A->Experiments.UsbLive
                  : Item == ITEM_QUIESCE ? A->Experiments.Quiesce
                                         : A->Experiments.PanelLinesOff;
    View->Label = Item == ITEM_USB_LIVE  ? gStrings->DbgUsbLive
                  : Item == ITEM_QUIESCE ? gStrings->DbgQuiesce
                                         : gStrings->DbgPanelLines;
    View->Verb  = gStrings->VerbChange;
    AsciiStrCopy (Value, Cap, On ? gStrings->On : gStrings->Off);
    break;
  }
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
  EXPERIMENTS *Experiments = &A->Experiments;

  switch (Item) {
  case ITEM_PORTAREOS:
    return BeginBoot (A, BOOT_TARGET_PORTAREOS);
  case ITEM_ANDROID:
    return BeginBoot (A, BOOT_TARGET_ANDROID);
  case ITEM_BOOT_ORDER:
    SettingsToggleBootMode (&A->Settings, &A->Caps);
    return Command (COMMAND_SAVE_SETTINGS, 0);
  case ITEM_UNINSTALL:
    A->Screen     = SCREEN_CONFIRM;
    A->Confirm    = CONFIRM_UNINSTALL;
    A->ConfirmYes = FALSE;
    return None ();
  case ITEM_EDL:
    A->Screen     = SCREEN_CONFIRM;
    A->Confirm    = CONFIRM_EDL;
    A->ConfirmYes = FALSE;
    return None ();
  case ITEM_RESTART:
    return Command (COMMAND_REBOOT, REBOOT_NORMAL);
  case ITEM_POWER_OFF:
    return Command (COMMAND_POWER_OFF, 0);
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
  A->FocusItem = ITEM_PORTAREOS;
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
  A->FocusItem = ITEM_PORTAREOS;
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

// Up and down switch between Cancel and the action; Back cancels.
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
