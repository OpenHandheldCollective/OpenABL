/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Splash - keeping the firmware's splash memory out of the kernel's hands.

  The display stays on through the hand-over unless a display-off is asked
  for, so the memory it scans out is reserved in the kernel's devicetree.
*/

#ifndef __EFI_SPLASH_H__
#define __EFI_SPLASH_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Core/DeviceTree.h"
#include "Efi/Serial.h"

// Whether the tree reserves the splash, as Android's and some Linux trees do.
BOOLEAN
SplashHasRegion (
  IN CONST UINT8 *Fdt,
  IN UINTN        Cap
);

// Reserves XBL's display region, or else the framebuffer's pages, when the
// tree has no splash_region or cont_splash_region. The display stays on
// through the hand-over, so a kernel that allocated that memory would show
// its contents. The node is named splash_region so the ABL's splash step
// updates it as the stock ABL does Android's. Skipped when a display-off is
// asked for.
VOID
SplashReserveSplash (
  IN OUT UINT8 *Fdt,
  IN UINTN      Cap,
  IN LOG_SINK   Log
);

#endif
