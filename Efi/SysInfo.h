/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SysInfo - the system facts on the menu's info card.

  Sizes are shown as sold (Core/SysInfoText). Each function returns a string
  the caller frees, or NULL when the fact isn't known or memory ran out.
*/

#ifndef __EFI_SYS_INFO_H__
#define __EFI_SYS_INFO_H__

#include "Base/OablBase.h"

// Returns the SoC's Snapdragon name for ChipInfo's id, else ChipInfo's own
// name for it.
CHAR8 *
SysInfoSoc (VOID);

// Returns the RAM size: the system and reserved memory in XBL's resource
// HOBs.
CHAR8 *
SysInfoRam (VOID);

// Returns the size of the fixed raw disks with media present.
CHAR8 *
SysInfoStorage (VOID);

// Returns the size of the removable raw disks with media present: the SD
// card, and a USB drive while USB host is up.
CHAR8 *
SysInfoSdCard (VOID);

// Returns the byte counts behind SysInfoRam, SysInfoStorage and
// SysInfoSdCard.
VOID
SysInfoRawSizes (
  OUT UINT64 *Ram,
  OUT UINT64 *Storage,
  OUT UINT64 *Card
);

// Returns the panel XBL drives on a Width x Height framebuffer: the one its
// DisplayPanelConfiguration variable names, else the only supported panel
// of that size.
CHAR8 *
SysInfoPanel (
  IN UINTN Width,
  IN UINTN Height
);

// Returns the Android build of the device's firmware: the system, vendor or
// boot fingerprint that AVB signs into the vbmeta images, read from the
// vbmeta partitions on the fixed disks.
CHAR8 *
SysInfoAndroidBuild (VOID);

#endif
