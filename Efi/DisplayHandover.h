/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DisplayHandover - switches the display off through XBL's display power
  protocol at ExitBootServices, or leaves it on. Nothing here allocates then.
*/

#ifndef __EFI_DISPLAY_HANDOVER_H__
#define __EFI_DISPLAY_HANDOVER_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Protocol/OpenAblMenuHost.h>

// Returns the DisplayQuiesce byte, or 0 if it isn't set. Without
// OABL_DISPLAY_QUIESCE the display-off bits (QUIESCE, LINES_OFF) read clear.
UINT8
DisplayHandoverQuiesceSetting (VOID);

// Switches the display off through XBL's display power protocol
VOID
DisplayHandoverQuiesce (IN OPENABL_STORAGE_LOG Log OPTIONAL);

#ifdef OABL_DISPLAY_QUIESCE
// Switches the backlight off for the selected device model. Do this before
// the display-off so XBL's DSI stop and the kernel's panel reset happen in
// the dark.
VOID
DisplayHandoverBacklightOff (IN OPENABL_STORAGE_LOG Log OPTIONAL);

// Drives the panel's supplies and enables off. On most SoCs the display power
// protocol leaves them on, so call this after DisplayHandoverQuiesce.
VOID
DisplayHandoverSuppliesOff (IN OPENABL_STORAGE_LOG Log OPTIONAL);
#endif

// The hand-over, run at ExitBootServices. KeepAlive leaves the display on,
// unless DisplayQuiesce forces it off.
VOID
DisplayHandoverBeforeExit (
  IN BOOLEAN             KeepAlive,
  IN OPENABL_STORAGE_LOG Log OPTIONAL
);

VOID
DisplayHandoverArm (IN OPENABL_STORAGE_LOG Log OPTIONAL);

// Forgets the last tree's say: the display then follows the build's default
// at ExitBootServices, on, or off with park-display.
VOID
DisplayHandoverReset (VOID);

// What the tree says: with a splash region the display stays on, without
// one it is switched off at ExitBootServices.
VOID
DisplayHandoverKeepSplash (IN BOOLEAN Keep);

#endif
