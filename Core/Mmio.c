/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Mmio - Register access through a table of functions.
*/

#include "Core/Mmio.h"

STATIC UINT32
HwRead32 (
  IN CONST MMIO *M,
  IN UINT64      Addr
)
{
  (VOID)M;
  return *(volatile UINT32 *)(UINTN)Addr;
}

STATIC VOID
HwWrite32 (
  IN CONST MMIO *M,
  IN UINT64      Addr,
  IN UINT32      Value
)
{
  (VOID)M;
  *(volatile UINT32 *)(UINTN)Addr = Value;
}

CONST MMIO gMmioHardware = {HwRead32, HwWrite32, 0};
