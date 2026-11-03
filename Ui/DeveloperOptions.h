/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DeveloperOptions - the menu's Developer options item and the list of
  switches it opens (SCREEN_DEVELOPER_OPTIONS). Used by Ui/App.c.
*/

#ifndef __UI_DEVELOPER_OPTIONS_H__
#define __UI_DEVELOPER_OPTIONS_H__

#include "Ui/Ui.h"

// Fills Rows with the developer options' header and items and returns the
// count. Handhelds have no back key, so the list ends with a Back row.
UINTN
DeveloperOptionsRows (
  IN CONST APP *A,
  OUT ROW      *Rows
);

// Describes ITEM_DEVELOPER_OPTIONS, a switch or the Back row. View comes in
// with the defaults set.
VOID
DeveloperOptionsItemView (
  IN CONST APP  *A,
  IN ITEM        Item,
  OUT ITEM_VIEW *View
);

// What Select does on ITEM_DEVELOPER_OPTIONS, a switch or the Back row.
COMMAND
DeveloperOptionsActivate (
  IN OUT APP *A,
  IN ITEM     Item
);

// Back from the developer options to the menu, on the item that opens them.
VOID
DeveloperOptionsLeave (IN OUT APP *A);

// Presses on the bootloader version in Device info that unlock the
// Developer options item.
#define DEVELOPER_OPTIONS_UNLOCK_PRESSES 7

// Counts a press on the bootloader version while the developer options are
// locked. Fills Toast with what to show ("" for nothing) and, on the press
// that unlocks them, returns the command that saves it.
COMMAND
DeveloperOptionsUnlockPress (
  IN OUT APP *A,
  OUT CHAR8  *Toast,
  IN UINTN    Cap
);

#endif
