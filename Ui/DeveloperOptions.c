/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DeveloperOptions - the developer options (Ui/DeveloperOptions.h).
*/

#include "Ui/DeveloperOptions.h"

#include "Base/Text.h"
#include "Ui/Lang.h"

// The countdown toasts start once this many presses are left.
#define COUNTDOWN_FROM 5

UINTN
DeveloperOptionsRows (
  IN CONST APP *A,
  OUT ROW      *Rows
)
{
  UINTN Count = 0;

  Rows[Count++] = (ROW){.Header = gStrings->DeveloperOptions};
  if (A->Caps.UsbHost) {
    Rows[Count++] = (ROW){.Item = ITEM_USB_LIVE};
  }
  Rows[Count++] = (ROW){.Item = ITEM_QUIESCE};
  Rows[Count++] = (ROW){.Item = ITEM_PANEL_LINES};
  Rows[Count++] = (ROW){.Item = ITEM_BOOT_LOG};
  Rows[Count++] = (ROW){.Item = ITEM_SD_DIAGNOSTICS};
  Rows[Count++] = (ROW){.Item = ITEM_DEVELOPER_OPTIONS_BACK};
  return Count;
}

VOID
DeveloperOptionsItemView (
  IN CONST APP  *A,
  IN ITEM        Item,
  OUT ITEM_VIEW *View
)
{
  CONST SETTINGS *Settings = &A->Settings;
  BOOLEAN         On;

  switch (Item) {
  case ITEM_DEVELOPER_OPTIONS:
    View->Icon     = ICON_CPU;
    View->Label    = gStrings->DeveloperOptions;
    View->Trailing = TRAILING_NAVIGATE;
    return;
  case ITEM_DEVELOPER_OPTIONS_BACK:
    View->Icon     = ICON_ARROW_LEFT;
    View->Label    = gStrings->Back;
    View->Trailing = TRAILING_NONE;
    return;
  case ITEM_USB_LIVE:
    On          = Settings->UsbHostLive;
    View->Icon  = ICON_USB;
    View->Label = gStrings->DeveloperOptionsUsbLive;
    break;
  case ITEM_QUIESCE:
    On          = Settings->DisplayOff;
    View->Icon  = ICON_MONITOR_OFF;
    View->Label = gStrings->DeveloperOptionsQuiesce;
    break;
  case ITEM_PANEL_LINES:
    On          = Settings->PanelLinesOff;
    View->Icon  = ICON_PLUG;
    View->Label = gStrings->DeveloperOptionsPanelLines;
    break;
  case ITEM_BOOT_LOG:
    On          = Settings->BootLog;
    View->Icon  = ICON_TERMINAL;
    View->Label = gStrings->DeveloperOptionsBootLog;
    break;
  default:
    On          = Settings->SdDiagnostics;
    View->Icon  = ICON_SD_CARD;
    View->Label = gStrings->DeveloperOptionsSdDiagnostics;
    break;
  }
  AsciiStrCopy (
    View->Value, sizeof (View->Value), On ? gStrings->On : gStrings->Off
  );
}

VOID
DeveloperOptionsLeave (IN OUT APP *A)
{
  A->Screen    = SCREEN_MENU;
  A->FocusItem = ITEM_DEVELOPER_OPTIONS;
}

COMMAND
DeveloperOptionsActivate (
  IN OUT APP *A,
  IN ITEM     Item
)
{
  SETTINGS *Settings = &A->Settings;

  switch (Item) {
  case ITEM_DEVELOPER_OPTIONS:
    A->Screen               = SCREEN_DEVELOPER_OPTIONS;
    A->DeveloperOptionsList = (LIST_STATE){0};
    A->FocusItem            = A->Caps.UsbHost ? ITEM_USB_LIVE : ITEM_QUIESCE;
    return (COMMAND){.Kind = COMMAND_NONE};
  case ITEM_DEVELOPER_OPTIONS_BACK:
    DeveloperOptionsLeave (A);
    return (COMMAND){.Kind = COMMAND_NONE};
  case ITEM_USB_LIVE:
    Settings->UsbHostLive = !Settings->UsbHostLive;
    break;
  case ITEM_QUIESCE:
    Settings->DisplayOff = !Settings->DisplayOff;
    break;
  case ITEM_PANEL_LINES:
    Settings->PanelLinesOff = !Settings->PanelLinesOff;
    break;
  case ITEM_BOOT_LOG:
    Settings->BootLog = !Settings->BootLog;
    break;
  default:
    Settings->SdDiagnostics = !Settings->SdDiagnostics;
    break;
  }
  return (COMMAND){.Kind = COMMAND_SAVE_SETTINGS};
}

COMMAND
DeveloperOptionsUnlockPress (
  IN OUT APP *A,
  OUT CHAR8  *Toast,
  IN UINTN    Cap
)
{
  UINTN Left;
  CHAR8 Count[8];

  Toast[0] = 0;
  A->UnlockPresses++;
  Left = DEVELOPER_OPTIONS_UNLOCK_PRESSES - A->UnlockPresses;
  if (Left == 0) {
    A->UnlockPresses             = 0;
    A->Settings.DeveloperOptions = TRUE;
    AsciiStrCopy (Toast, Cap, gStrings->DeveloperOptionsUnlocked);
    return (COMMAND){.Kind = COMMAND_SAVE_SETTINGS};
  }
  if (Left == 1) {
    AsciiStrCopy (Toast, Cap, gStrings->DeveloperOptionsStepAway);
  } else if (Left <= COUNTDOWN_FROM) {
    CONST CHAR8 *Arg = Count;

    AsciiStrFromU64 (Count, sizeof (Count), Left);
    LangFill (Toast, Cap, gStrings->DeveloperOptionsStepsAway, &Arg, 1);
  }
  return (COMMAND){.Kind = COMMAND_NONE};
}
