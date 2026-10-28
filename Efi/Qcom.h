/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Qcom - calls into Qualcomm's XBL protocols, made as LinuxLoader makes them.

  Layouts come from the ABL's QcomModulePkg/Include/Protocol headers. Only
  the two whose types appear here are included, since EFIVerifiedBoot.h's
  enum values (READ_CONFIG, GREEN, RED, ...) would reach every includer.
  XBL installs ChipInfo under b0760469-970c-487a-a4b5-28db7b45cef1, not the
  61224fbe-b240-4d53-b661-a9a1f343b05c that EFIChipInfo.h defines.
*/

#ifndef __EFI_QCOM_H__
#define __EFI_QCOM_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Protocol/EFIPlatformInfo.h>
#include <Protocol/EFIPmicVersion.h>

#include "Base/Buffer.h"
#include "Base/Text.h"
#include "Core/Carveouts.h"

// A ChipInfo call's status and value.
typedef struct _QCOM_ANSWER {
  EFI_STATUS Status;
  UINT32     Value;
} QCOM_ANSWER;

// ChipInfo's 16-byte name with every byte possibly replaced by U+FFFD
// (3 bytes in UTF-8), plus a NUL.
#define QCOM_CHIP_NAME_SIZE 49

typedef struct _QCOM_CHIP {
  UINT64      Revision;
  QCOM_ANSWER Id;
  QCOM_ANSWER RawId;
  // Major << 16 | minor.
  QCOM_ANSWER Version;
  EFI_STATUS  NameStatus;
  // Up to the first NUL, invalid UTF-8 replaced.
  CHAR8 Name[QCOM_CHIP_NAME_SIZE];
} QCOM_CHIP;

// Asks ChipInfo for the id, raw id, version and name, in that order.
// FALSE without the protocol.
BOOLEAN
QcomChip (OUT QCOM_CHIP *Out);

// Reads the board's platform, version (major << 16 | minor), subtype,
// fusion and OEM variant from PlatformInfo. FALSE without the protocol or
// if the call fails.
BOOLEAN
QcomPlatform (OUT EFI_PLATFORMINFO_PLATFORM_INFO_TYPE *Out);

// Layouts XBL fills in.
_Static_assert (
  sizeof (EFI_PLATFORMINFO_PLATFORM_INFO_TYPE) == 20,
  "EFI_PLATFORMINFO_PLATFORM_INFO_TYPE"
);
_Static_assert (
  sizeof (EFI_PM_DEVICE_INFO_TYPE) == 16,
  "EFI_PM_DEVICE_INFO_TYPE"
);

// PMIC indices tried.
#define QCOM_PMICS_MAX 8

// Reads PMIC info by index from 0 until GetPmicInfo fails, skipping models
// 0 (unknown) and 0xFFFF and up (invalid). Returns the count; 0 without the
// protocol.
UINTN
QcomPmics (OUT EFI_PM_DEVICE_INFO_TYPE Out[QCOM_PMICS_MAX]);

// A storage device, as XBL's driver for it describes it.
typedef struct _QCOM_CARD {
  // "UFS", "MMC", "SD"...
  CHAR8 *Kind;
  UINT16 MfrId;
  UINT16 OemId;
  // SCSI inquiry (UFS vendor, part, revision); empty on SDCC.
  CHAR8 *Inquiry;
  // From the handle's BlockIo; set for SD cards.
  BOOLEAN Removable;
} QCOM_CARD;

typedef struct _QCOM_CARDS OABL_VEC (QCOM_CARD) QCOM_CARDS;

// Appends every storage device whose card-info protocol answers. Texts end
// at a NUL, have invalid UTF-8 replaced and their words joined by single
// spaces.
VOID
QcomCards (IN OUT QCOM_CARDS *Out);

VOID
QcomCardsFree (IN OUT QCOM_CARDS *Cards);

// Returns XBL's DisplayPanelConfiguration variable (Qualcomm token space,
// 882f8c2b-9646-435f-8de5-f208ff80c1bd), the kernel command-line fragment
// for the panel it detected, trimmed. Caller frees it; NULL if not set.
CHAR8 *
QcomDisplayPanelConfig (VOID);

// DRAM banks as (base, available length).
typedef struct _QCOM_BANKS OABL_VEC (CARVEOUTS_REGION) QCOM_BANKS;

// Reads the DRAM banks with GetRamPartitions, which must first answer
// EFI_BUFFER_TOO_SMALL with the count. FALSE without the protocol or with
// no banks. Caller frees Out with VecFree.
BOOLEAN
QcomRamBanks (OUT QCOM_BANKS *Out);

// Reads Length bytes of the devinfo record with VerifiedBoot's
// VBRwDeviceState (READ_CONFIG), and the protocol's revision. Out is
// allocated whatever *Status says; caller frees it with BufferFree. FALSE
// without the protocol or out of memory.
BOOLEAN
QcomReadDeviceState (
  IN UINTN         Length,
  OUT UINT64      *Revision,
  OUT EFI_STATUS  *Status,
  OUT OABL_BUFFER *Out
);

// Finds how XBL rotates the picture on a Width x Height framebuffer, in
// degrees, from its panel XMLs. FALSE off Qualcomm firmware or if no panel
// has that size.
BOOLEAN
QcomFirmwarePanelRotation (
  IN UINTN    Width,
  IN UINTN    Height,
  OUT UINT16 *Out
);

// Panel XMLs, pointing into XBL's memory.
typedef struct _QCOM_XMLS OABL_VEC (OABL_BYTES) QCOM_XMLS;

// Lists the XML of every panel XBL supports, asking Display Utils'
// GetProperty (EFI_DISPLAY_UTILS_PANEL_CONFIG) by index until it fails. The
// bytes stay XBL's until ExitBootServices; free the array with VecFree.
// FALSE without Display Utils.
BOOLEAN
QcomPanelXmls (OUT QCOM_XMLS *Out);

#endif
