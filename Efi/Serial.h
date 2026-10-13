/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Serial - the serial debug log, and the log sink the Efi modules pass
  around.

  Lines go straight to the first serial port, not the text console, so they
  never draw over the UI.
*/

#ifndef __EFI_SERIAL_H__
#define __EFI_SERIAL_H__

#include <stdarg.h>
#include "Base/OablBase.h"

// Writes the text and "\r\n" to the first serial port, if there is one.
VOID
SerialWriteLine (
  IN CONST CHAR8 *Format,
  ...
)
  __attribute__ ((format (
    printf,
    1,
    2
  )));

VOID
SerialVWriteLine (
  IN CONST CHAR8 *Format,
  IN va_list      Args
);

#ifdef OABL_DEBUG
// From now on, also writes the lines to the text console (debug builds).
VOID
SerialToScreen (VOID);
#endif

// Writes "[oabl] " and the text as one line.
VOID
SerialLog (
  IN CONST CHAR8 *Format,
  ...
)
  __attribute__ ((format (
    printf,
    1,
    2
  )));

// Where a module's log lines go. Passed by value. Fn gets each line
// NUL-terminated, valid only for the call. A NULL Fn drops every line.
typedef struct _LOG_SINK {
  VOID (*Fn) (
    IN VOID        *Context OPTIONAL,
    IN CONST CHAR8 *Line
  );
  VOID *Ctx;
} LOG_SINK;

#define LOG_QUIET ((LOG_SINK){NULL, NULL})

STATIC inline VOID
LogLine (
  IN LOG_SINK     Log,
  IN CONST CHAR8 *Line
)
{
  if (Log.Fn) {
    Log.Fn (Log.Ctx, Line);
  }
}

// Writes a formatted line to Log. Nothing is logged if allocating the text
// fails.
VOID
LogPrint (
  IN LOG_SINK     Log,
  IN CONST CHAR8 *Format,
  ...
)
  __attribute__ ((format (
    printf,
    2,
    3
  )));

#endif
