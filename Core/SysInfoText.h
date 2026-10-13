/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SysInfoText - Text for the system facts on the menu's info card.
*/

#ifndef __CORE_SYS_INFO_TEXT_H__
#define __CORE_SYS_INFO_TEXT_H__

#include "Base/OablBase.h"

#include "Base/Text.h"

// Each field is NULL or allocated with AllocatePool.
typedef struct _SYSINFO {
  CHAR8 *Soc;
  CHAR8 *Ram;
  CHAR8 *Storage;
  CHAR8 *SdCard;
  CHAR8 *Firmware;
} SYS_INFO;

VOID
SysInfoFree (IN OUT SYS_INFO *Info);

// Formats a size as "12 GB", "1.5 GB" or "512 MB", in binary units.
CHAR8 *
SysInfoFormatBytes (IN UINT64 Bytes);

// Names an EFI_PLATFORMINFO_TYPE_* board type, or returns NULL.
CONST CHAR8 *
SysInfoPlatformName (IN UINT32 Type);

CONST CHAR8 *
SysInfoSocName (IN UINT32 ChipId);

// Extracts the panel name from XBL's DisplayPanelConfiguration, such as
// "msm_drm.dsi_display0=qcom,mdss_dsi_panel_ar14_1080p_144hz_video:...",
// without the "qcom," and "mdss_dsi_" prefixes.
CHAR8 *
SysInfoPanelFromCmdline (IN OABL_TEXT PanelConfig);

// Returns the value of the first of Keys in an AVB vbmeta image. Property
// descriptors are stored as key, NUL, value, NUL.
CHAR8 *
SysInfoVbmetaProperty (
  IN CONST UINT8        *Image,
  IN UINTN               Size,
  IN CONST CHAR8 *CONST *Keys,
  IN UINTN               KeyCount
);

// Returns the RAM size as sold. The firmware reports less, since some of
// it is reserved.
CHAR8 *
SysInfoRamSize (IN UINT64 Bytes);

CHAR8 *
SysInfoStorageSize (IN UINT64 Bytes);

// SD cards also come in sizes between powers of two, such as 400 GB.
CHAR8 *
SysInfoCardSize (IN UINT64 Bytes);

#endif
