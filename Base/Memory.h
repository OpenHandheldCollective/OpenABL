/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Memory - pool allocation, memory copies and CRC-32.

  The names follow edk2's MemoryAllocationLib, BaseMemoryLib and BaseLib.
  Rt/Rt.c implements them with boot services, Host/Host.c with the C
  library.
*/

#ifndef __BASE_MEMORY_H__
#define __BASE_MEMORY_H__

#include "Base/OablBase.h"

VOID *
AllocatePool (IN UINTN Size);

// Buffer may be NULL.
VOID
FreePool (IN VOID *Buffer OPTIONAL);

// Works like C's realloc.
VOID *
ResizePool (
  IN VOID *Buffer OPTIONAL,
  IN UINTN Size
);

// Allocates Count x Size zeroed bytes. Returns NULL if that overflows.
VOID *
AllocateZeroArray (
  IN UINTN Count,
  IN UINTN Size
);

STATIC inline VOID *
AllocateZeroPool (IN UINTN Size)
{
  return AllocateZeroArray (1, Size);
}

STATIC inline VOID *
CopyMem (
  OUT VOID      *Destination,
  IN CONST VOID *Source,
  IN UINTN       Length
)
{
  return __builtin_memmove (Destination, Source, Length);
}

STATIC inline VOID *
SetMem (
  OUT VOID *Buffer,
  IN UINTN  Length,
  IN UINT8  Value
)
{
  return __builtin_memset (Buffer, Value, Length);
}

STATIC inline VOID *
ZeroMem (
  OUT VOID *Buffer,
  IN UINTN  Length
)
{
  return __builtin_memset (Buffer, 0, Length);
}

STATIC inline INT32
CompareMem (
  IN CONST VOID *A,
  IN CONST VOID *B,
  IN UINTN       Length
)
{
  return __builtin_memcmp (A, B, Length);
}

// Allocates at least one byte, so an empty copy isn't NULL.
STATIC inline VOID *
AllocateCopyPool (
  IN UINTN       Size,
  IN CONST VOID *Buffer
)
{
  VOID *Copy = AllocatePool (Size ? Size : 1);
  if (Copy) {
    CopyMem (Copy, Buffer, Size);
  }
  return Copy;
}

// The IEEE 802.3 CRC-32, as used by GPT and zlib.
UINT32
CalculateCrc32 (
  IN CONST VOID *Buffer,
  IN UINTN       Length
);

#endif
