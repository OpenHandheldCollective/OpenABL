/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Sha2 - SHA-256 and SHA-384.
*/

#ifndef __CORE_SHA2_H__
#define __CORE_SHA2_H__

#include "Base/OablBase.h"

#define SHA256_SIZE 32
#define SHA384_SIZE 48

VOID
Sha256 (
  IN CONST VOID *Data,
  IN UINTN       Size,
  OUT UINT8      Digest[SHA256_SIZE]
);

VOID
Sha384 (
  IN CONST VOID *Data,
  IN UINTN       Size,
  OUT UINT8      Digest[SHA384_SIZE]
);

#endif
