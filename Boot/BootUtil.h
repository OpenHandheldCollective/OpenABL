/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootUtil - logging, device path and variable helpers for the boot code.
*/

#ifndef __BOOT_BOOT_UTIL_H__
#define __BOOT_BOOT_UTIL_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Base/Text.h"

// Fits the longest StatusDebug text, "Status(18446744073709551615)".
#define BOOT_STATUS_DEBUG_SIZE 32

// Writes the status name to Out, such as NOT_FOUND, or Status(N) if it has
// none. Returns Out, so the call can be a format argument:
// StatusDebug (Status, (CHAR8[BOOT_STATUS_DEBUG_SIZE]){0}).
CONST CHAR8 *
StatusDebug (
  IN EFI_STATUS Status,
  OUT CHAR8     Out[BOOT_STATUS_DEBUG_SIZE]
);

// Appends Text to String in double quotes. Escapes \\, ", \n, \r, \t and \0,
// and other control characters as \u{..}.
VOID
StringDebugStr (
  IN OUT OABL_STRING *String,
  IN CONST CHAR8     *Text
);

// Like StringDebugStr, but appends Some("...") or None for NULL.
VOID
StringDebugOptStr (
  IN OUT OABL_STRING *String,
  IN CONST CHAR8     *Text OPTIONAL
);

// Appends Items to String as a list of quoted strings, ["a", "b"].
VOID
StringDebugStrs (
  IN OUT OABL_STRING *String,
  IN CHAR8 *CONST    *Items,
  IN UINTN            Count
);

// Returns Text quoted as StringDebugStr does. Caller frees the result.
CHAR8 *
DebugStr (IN CONST CHAR8 *Text);

// Returns TRUE if the Size bytes at Path are whole device path nodes ending
// in an end-of-entire-path node.
BOOLEAN
DevicePathValid (
  IN CONST UINT8 *Path,
  IN UINTN        Size
);

// Returns the size of the device path at Path, including its end node.
// Returns 0 for NULL or a malformed path.
UINTN
DevicePathSize (IN CONST VOID *Path OPTIONAL);

// Opens a protocol exclusively, with our image as the agent. Close it with
// CloseExclusive.
EFI_STATUS
OpenExclusive (
  IN EFI_HANDLE Handle,
  IN EFI_GUID  *Guid,
  OUT VOID    **Out
);

VOID
CloseExclusive (
  IN EFI_HANDLE Handle,
  IN EFI_GUID  *Guid
);

// Reads a variable into Out. Caller frees Out.
EFI_STATUS
GetVariable (
  IN CONST CHAR8  *Name,
  IN EFI_GUID     *Vendor,
  OUT OABL_BUFFER *Out
);

// Sets a variable, or deletes it when Attributes and Size are 0.
EFI_STATUS
SetVariable (
  IN CONST CHAR8 *Name,
  IN EFI_GUID    *Vendor,
  IN UINT32       Attributes,
  IN CONST VOID  *Data OPTIONAL,
  IN UINTN        Size
);

STATIC inline EFI_STATUS
DeleteVariable (
  IN CONST CHAR8 *Name,
  IN EFI_GUID    *Vendor
)
{
  return SetVariable (Name, Vendor, 0, NULL, 0);
}

#endif
