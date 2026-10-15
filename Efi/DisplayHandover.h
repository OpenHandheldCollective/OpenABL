/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DisplayHandover - turns the display off, or not, before the OS starts.

  The ABL calls DisplayHandoverBeforeExit just before ExitBootServices.
  Unless the kernel's devicetree keeps the splash, the DisplayQuiesce
  setting can switch the backlight off, park the display and drive the
  panel supplies off. The display-off code is built only with
  OABL_DISPLAY_QUIESCE or OABL_PARK_DISPLAY. Nothing here allocates, since it
  may run inside ExitBootServices.
*/

#ifndef __EFI_DISPLAY_HANDOVER_H__
#define __EFI_DISPLAY_HANDOVER_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Protocol/OpenAblMenuHost.h>

#if defined(OABL_DISPLAY_QUIESCE) || defined(OABL_PARK_DISPLAY)
#define OABL_DISPLAY_OFF
#endif

// Returns the DisplayQuiesce byte, or 0 if it isn't set. Without
// OABL_DISPLAY_QUIESCE the display-off bits (QUIESCE, LINES_OFF) read clear.
// OABL_PARK_DISPLAY sets QUIESCE unless a byte is stored.
UINT8
DisplayHandoverQuiesceSetting (VOID);

#ifdef OABL_DISPLAY_OFF
// Parks the display: off through XBL's display power protocol, then its
// clocks off (the MDP clock moved to XO first), then the MDSS core GDSC.
VOID
DisplayHandoverQuiesce (IN OPENABL_STORAGE_LOG Log OPTIONAL);

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

// Called right before ExitBootServices on the boot-image path. KeepAlive is
// TRUE when the ABL finds a splash region in the kernel's devicetree. A
// display-off requested in DisplayQuiesce overrides it.
VOID
DisplayHandoverBeforeExit (
  IN BOOLEAN             KeepAlive,
  IN OPENABL_STORAGE_LOG Log OPTIONAL
);

#endif
