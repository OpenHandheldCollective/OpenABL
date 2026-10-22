/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Splash - hands XBL's splash over to the kernel's devicetree, as Qualcomm's
  ABL does for Android's.

  A tree with a splash region keeps the display on and gets the region
  pointed at XBL's framebuffer; one without has the display switched off
  before Linux (Efi/DisplayHandover.h).
*/

#ifndef __EFI_SPLASH_H__
#define __EFI_SPLASH_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Core/DeviceTree.h"
#include "Efi/Serial.h"

// Gets the pages of the panel's framebuffer: the graphics output whose
// framebuffer is in XBL's display region, else the first one.
BOOLEAN
SplashFramebuffer (
  OUT UINT64 *Base,
  OUT UINT64 *Length
);

// Points the tree's splash region at XBL's framebuffer when its reg is the
// <0 addr 0 size> the stock ABL patches, in place and without growing the
// tree. TRUE if the tree has a splash region (display stays on), else FALSE
// (switched off before Linux); logging allocates, so not at ExitBootServices.
BOOLEAN
SplashHandOver (
  IN OUT UINT8 *Fdt,
  IN UINTN      Cap,
  IN LOG_SINK   Log
);

#endif
