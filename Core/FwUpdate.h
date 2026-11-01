/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  FwUpdate - decides whether a staged ABL image can replace the installed
  one. XBL never falls back to the other slot, so anything it wouldn't load
  is refused. Efi/FwUpdate does the I/O.
*/

#ifndef __CORE_FW_UPDATE_H__
#define __CORE_FW_UPDATE_H__

#include "Base/OablBase.h"

#include "Base/Text.h"
#include "Core/AblImage.h"
#include "Core/Sha2.h"

// ESRT LastAttemptStatus values.
#define FW_UPDATE_SUCCESS            0u
#define FW_UPDATE_ERROR_UNSUCCESSFUL 1u
#define FW_UPDATE_ERROR_RESOURCES    2u
#define FW_UPDATE_ERROR_VERSION      3u
#define FW_UPDATE_ERROR_FORMAT       4u
#define FW_UPDATE_ERROR_AUTH         5u
#define FW_UPDATE_ERROR_BATTERY      7u

// The FirmwareUpdate setting: LE32 version, LE32 status.
#define FW_UPDATE_RECORD_SIZE 8

CONST CHAR8 *
FwUpdateStatusName (IN UINT32 Status);

// Parses a sha256sum line. FALSE if it's malformed or for another file.
BOOLEAN
FwUpdateSha256Line (
  IN OABL_TEXT    Line,
  IN CONST CHAR8 *Name,
  OUT UINT8       Digest[SHA256_SIZE]
);

typedef struct _FW_UPDATE_INPUT {
  OABL_BYTES   Staged;
  CONST CHAR8 *Name;
  // Empty if there's no .sha256.
  OABL_TEXT Sha256;
  // The active slot's abl.
  OABL_BYTES Installed;
  // The smaller of the two abl partitions.
  UINT64  PartitionSize;
  BOOLEAN Force;
} FW_UPDATE_INPUT;

typedef struct _FW_UPDATE_CHECK {
  UINT32 Status;
  // major << 16 | minor
  UINT32    Version;
  BOOLEAN   AlreadyInstalled;
  ABL_IMAGE New;
  ABL_IMAGE Old;
  CHAR8     Why[96];
} FW_UPDATE_CHECK;

VOID
FwUpdateCheck (
  IN CONST FW_UPDATE_INPUT *In,
  OUT FW_UPDATE_CHECK      *Out
);

#endif
