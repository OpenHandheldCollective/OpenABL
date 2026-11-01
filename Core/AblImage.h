/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  AblImage - parses a signed ABL ELF and its MBN hash segment.

  MBN header offsets (LE32):
    v3/5/6  4 version, 8/12 qcom sig/chain (v5+), 20 hash table size,
            28 sig, 36 chain, 40/44 qcom/oem metadata (v6). 40 bytes, 48 on v6.
    v7      4 version, 8 common metadata, 12/16 qcom/oem metadata, 20 hash
            table size, 24/28 qcom sig/chain, 32/36 oem sig/chain, then the
            common metadata at 40: 48 sw id, 56 hash alg.

  Tag: "OABLIMG1", chipset[16], version[16], NUL padded.
*/

#ifndef __CORE_ABL_IMAGE_H__
#define __CORE_ABL_IMAGE_H__

#include "Base/OablBase.h"

#define ABL_IMAGE_TAG_MAGIC "OABLIMG1"
#define ABL_IMAGE_TAG_SIZE  40
#define ABL_IMAGE_TEXT_MAX  16

typedef enum {
  ABL_IMAGE_OK,
  ABL_IMAGE_NOT_ELF,
  ABL_IMAGE_NO_HASH_SEGMENT,
  ABL_IMAGE_BAD_HASH_SEGMENT,
  ABL_IMAGE_BAD_DIGEST,
  ABL_IMAGE_BAD_TAG,
} ABL_IMAGE_RESULT;

typedef struct _ABL_IMAGE {
  UINT8  Class;
  UINT16 Machine;
  UINT64 Entry;
  UINT32 MbnVersion;
  UINT32 DigestSize;
  // v7 only.
  UINT32 SoftwareId;
  UINT32 QcomSignatureSize;
  UINT32 QcomCertChainSize;
  UINT32 SignatureSize;
  UINT32 CertChainSize;
  // Bytes up to the end of the furthest segment.
  UINTN Size;
  // Empty if there's no tag.
  CHAR8 Chipset[ABL_IMAGE_TEXT_MAX + 1];
  CHAR8 Version[ABL_IMAGE_TEXT_MAX + 1];
} ABL_IMAGE;

// Data can run past the image, e.g. a whole partition.
ABL_IMAGE_RESULT
AblImageRead (
  IN CONST UINT8 *Data,
  IN UINTN        Size,
  IN BOOLEAN      CheckDigests,
  OUT ABL_IMAGE  *Out,
  OUT UINT32     *BadSegment OPTIONAL
);

CONST CHAR8 *
AblImageResultName (IN ABL_IMAGE_RESULT Result);

// NULL if XBL would load New the same way it loads Current, else what
// differs.
CONST CHAR8 *
AblImageMismatch (
  IN CONST ABL_IMAGE *Current,
  IN CONST ABL_IMAGE *New
);

#endif
