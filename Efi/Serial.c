/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Serial - the serial debug log, and the log sink the Efi modules pass
  around.
*/

#include "Efi/Serial.h"

#include "Efi/Efi.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"

// Returns the first serial port, or NULL. It is opened shared: the
// terminal driver keeps it open and this code only writes.
STATIC EFI_SERIAL_IO_PROTOCOL *
Port (VOID)
{
  EFI_HANDLE *Handles;
  UINTN       Count = LocateHandles (&gEfiSerialIoProtocolGuid, &Handles);
  EFI_SERIAL_IO_PROTOCOL *SerialIo =
    Count ? GetProtocol (Handles[0], &gEfiSerialIoProtocolGuid) : NULL;

  if (Handles) {
    gBS->FreePool (Handles);
  }
  return SerialIo;
}

#ifdef OABL_DEBUG
// Also on the text console, once SerialToScreen turned it on.
STATIC BOOLEAN mToScreen;

VOID
SerialToScreen (VOID)
{
  mToScreen = TRUE;
}

STATIC VOID
ToScreen (IN CONST OABL_STRING *Line)
{
  CHAR16 Wide[256];
  UINTN  Length = 0;

  if (!mToScreen || !gST->ConOut || !Line->Str) {
    return;
  }
  while (Length < Line->Len && Length + 1 < sizeof (Wide) / sizeof (*Wide)) {
    Wide[Length] = (CHAR16)(UINT8)Line->Str[Length];
    Length++;
  }
  Wide[Length] = 0;
  gST->ConOut->OutputString (gST->ConOut, Wide);
}
#endif

// Writes Line and "\r\n" to the serial port, then frees Line.
STATIC VOID
Emit (IN OUT OABL_STRING *Line)
{
  EFI_SERIAL_IO_PROTOCOL *SerialIo;
  UINTN                   Size;

  StringAppend (Line, "\r\n");
  if (Line->Str && (SerialIo = Port ())) {
    Size = Line->Len;
    SerialIo->Write (SerialIo, &Size, Line->Str);
  }
#ifdef OABL_DEBUG
  ToScreen (Line);
#endif
  StringFree (Line);
}

VOID
SerialVWriteLine (
  IN CONST CHAR8 *Format,
  IN va_list      Args
)
{
  OABL_STRING Line = {0};

  StringVPrint (&Line, Format, Args);
  Emit (&Line);
}

VOID
SerialWriteLine (
  IN CONST CHAR8 *Format,
  ...
)
{
  va_list Args;

  va_start (Args, Format);
  SerialVWriteLine (Format, Args);
  va_end (Args);
}

VOID
SerialLog (
  IN CONST CHAR8 *Format,
  ...
)
{
  OABL_STRING Line = {0};
  va_list     Args;

  StringAppend (&Line, "[oabl] ");
  va_start (Args, Format);
  StringVPrint (&Line, Format, Args);
  va_end (Args);
  Emit (&Line);
}

VOID
LogPrint (
  IN LOG_SINK     Log,
  IN CONST CHAR8 *Format,
  ...
)
{
  va_list Args;
  CHAR8  *Line;

  if (!Log.Fn) {
    return;
  }
  va_start (Args, Format);
  Line = VSPrintAlloc (Format, Args);
  va_end (Args);
  if (!Line) {
    return;
  }
  Log.Fn (Log.Ctx, Line);
  FreePool (Line);
}
