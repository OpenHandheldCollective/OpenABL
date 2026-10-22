/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DtFixup - EFI_DT_FIXUP_PROTOCOL (U-Boot's), which systemd-boot and GRUB
  2.12 and later call with the devicetree they load. The fix-up is the splash
  hand-over of Efi/Splash.h, ported from Propeller's display.rs.
*/

#ifndef __EFI_DT_FIXUP_H__
#define __EFI_DT_FIXUP_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Efi/Serial.h"

// Installs the protocol once. Later calls only move where the fix-ups log.
VOID
DtFixupInstall (IN LOG_SINK Log);

#endif
