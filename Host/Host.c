/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Host - Base/Memory.h for the host build (the simulator), on top of the C
  library.
*/

#include <stdlib.h>

#include "Base/Memory.h"

VOID *
AllocatePool (IN UINTN Size)
{
  return malloc (Size);
}

VOID *
ResizePool (
  IN VOID *Buffer OPTIONAL,
  IN UINTN Size
)
{
  return realloc (Buffer, Size);
}

VOID
FreePool (IN VOID *Buffer OPTIONAL)
{
  free (Buffer);
}

VOID *
AllocateZeroArray (
  IN UINTN Count,
  IN UINTN Size
)
{
  return calloc (Count, Size);
}

UINT32
CalculateCrc32 (
  IN CONST VOID *Buffer,
  IN UINTN       Length
)
{
  CONST UINT8 *Bytes = Buffer;
  UINT32       Crc   = ~0u;

  for (UINTN Index = 0; Index < Length; Index++) {
    Crc ^= Bytes[Index];
    for (INT32 Bit = 0; Bit < 8; Bit++) {
      Crc = Crc & 1 ? (Crc >> 1) ^ 0xedb88320u : Crc >> 1;
    }
  }
  return ~Crc;
}
