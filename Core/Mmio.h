/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Mmio - Register access through a table of functions.

  Hardware code takes a CONST MMIO * instead of dereferencing addresses, so
  host tests can pass a table that records each access. Delays are left to
  the caller so tests don't have to wait.
*/

#ifndef __CORE_MMIO_H__
#define __CORE_MMIO_H__

#include "Base/OablBase.h"

typedef struct _MMIO {
  UINT32 (*Read32) (
    IN CONST struct _MMIO *M,
    IN UINT64              Addr
  );
  VOID (*Write32) (
    IN CONST struct _MMIO *M,
    IN UINT64              Addr,
    IN UINT32              Value
  );
  // Implementation data, such as a test's register table.
  VOID *Ctx;
} MMIO;

STATIC inline UINT32
MmioRead32 (
  IN CONST MMIO *M,
  IN UINT64      Addr
)
{
  return M->Read32 (M, Addr);
}

STATIC inline VOID
MmioWrite32 (
  IN CONST MMIO *M,
  IN UINT64      Addr,
  IN UINT32      Value
)
{
  M->Write32 (M, Addr, Value);
}

// Direct volatile accesses, UEFI build only. Addresses must be mapped as
// device memory, as XBL's identity map does.
extern CONST MMIO gMmioHardware;

#endif
