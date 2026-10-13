/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Print - printf-style formatting without a C library.

  Supports d i u x X o c s p and %, the flags - 0 + space #, width and
  precision (also as *), and the lengths hh h l ll z j t. There is no
  floating point. A NULL %s prints "(null)".
*/

#ifndef __BASE_PRINT_H__
#define __BASE_PRINT_H__

#include <stdarg.h>
#include "Base/OablBase.h"

// Works like C's vsnprintf. Returns the length of the whole text, even if
// it was cut short.
INT32
VSPrint (
  OUT CHAR8      *Buffer OPTIONAL,
  IN UINTN        BufferSize,
  IN CONST CHAR8 *Format,
  IN va_list      Args
);

INT32
SPrint (
  OUT CHAR8      *Buffer OPTIONAL,
  IN UINTN        BufferSize,
  IN CONST CHAR8 *Format,
  ...
)
  __attribute__ ((format (
    printf,
    3,
    4
  )));

// Returns the text in a new pool allocation. Caller frees it.
CHAR8 *
SPrintAlloc (
  IN CONST CHAR8 *Format,
  ...
)
  __attribute__ ((format (
    printf,
    1,
    2
  )));

CHAR8 *
VSPrintAlloc (
  IN CONST CHAR8 *Format,
  IN va_list      Args
);

#endif
