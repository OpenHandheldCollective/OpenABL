/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Abl - the ABL's log console and the libraries the ABL shares with us.
*/

#ifndef __EFI_ABL_H__
#define __EFI_ABL_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"

// AblConsole: the ABL's DEBUG output reaches us through the storage
// protocol's LogSink. It is kept in a 32 KiB ring for the menu's Log screen
// and, in verbose mode, drawn the way the ABL's FrameBufferSerialPortLib drew
// it: the 5x12 font at twice its size, white on black, wrapping to the top
// without scrolling. The sink can run at any TPL inside a DEBUG call, so it
// allocates nothing and calls no boot service.

// Until the ABL hands us the sink, our own log lines are written to the
// console directly while this is on.
VOID
AblConsoleDirect (IN BOOLEAN On);

// Writes Line and "\r\n" to the console while AblConsoleDirect is on.
VOID
AblConsoleProgramLine (IN CONST CHAR8 *Line);

// LogSink for the ABL's serial-port library. Safe at any TPL. A NULL Buffer
// is ignored.
VOID EFIAPI
AblConsoleSink (
  IN CONST UINT8 *Buffer OPTIONAL,
  IN UINTN        Length
);

// Starts drawing the log on the GOP of gST->ConsoleOutHandle (verbose mode).
VOID
AblConsoleShow (VOID);

// Stops drawing the log (silent mode).
VOID
AblConsoleHide (VOID);

// Suspends drawing while the menu is on screen.
VOID
AblConsoleHold (IN BOOLEAN On);

// Copies at most the newest Max bytes of the log into Out, oldest first.
// Caller frees Out with BufferFree. Returns FALSE when out of memory.
BOOLEAN
AblConsoleRead (
  IN UINTN         Max,
  OUT OABL_BUFFER *Out
);

// AblLib: OPENABL_ABL_LIB_PROTOCOL (Protocol/OpenAblAblLib.h in the ABL)
// gives us Qualcomm's decompress for gzip kernels and libfdt for the
// devicetree editor. The libfdt functions in Core/LibFdt.h are defined here
// under their own names and forward to the ABL. Without the protocol they
// return -FDT_ERR_BADSTATE.

// Locates the ABL's library protocol. Call it before any devicetree edit or
// inflate. Returns FALSE if the ABL doesn't have it.
BOOLEAN
AblLibInit (VOID);

// Inflates the gzip stream In into Out with the ABL's decompress. OutSize
// must be larger than InSize and hold the whole output. Written gets the
// output size, Used the input consumed including the 8-byte trailer. Only
// the file name field of the gzip header is skipped. Returns EFI_LOAD_ERROR
// on failure, EFI_UNSUPPORTED without the protocol.
EFI_STATUS
AblLibInflate (
  IN CONST VOID *In,
  IN UINTN       InSize,
  OUT VOID      *Out,
  IN UINTN       OutSize,
  OUT UINTN     *Written,
  OUT UINTN     *Used
);

#endif
