/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootUtil - logging, device path and variable helpers for the boot code.
*/

#include "Boot/BootUtil.h"

#include "Base/Memory.h"
#include "Base/Print.h"

// UEFI status names without the EFI_ prefix, indexed by code - 1. Error
// codes 29 and 30 have no name.
STATIC CONST CHAR8 *CONST mWarnings[] = {
  "WARN_UNKNOWN_GLYPH",    "WARN_DELETE_FAILURE", "WARN_WRITE_FAILURE",
  "WARN_BUFFER_TOO_SMALL", "WARN_STALE_DATA",     "WARN_FILE_SYSTEM",
  "WARN_RESET_REQUIRED",
};

STATIC CONST CHAR8 *CONST mErrors[] = {
  "LOAD_ERROR",
  "INVALID_PARAMETER",
  "UNSUPPORTED",
  "BAD_BUFFER_SIZE",
  "BUFFER_TOO_SMALL",
  "NOT_READY",
  "DEVICE_ERROR",
  "WRITE_PROTECTED",
  "OUT_OF_RESOURCES",
  "VOLUME_CORRUPTED",
  "VOLUME_FULL",
  "NO_MEDIA",
  "MEDIA_CHANGED",
  "NOT_FOUND",
  "ACCESS_DENIED",
  "NO_RESPONSE",
  "NO_MAPPING",
  "TIMEOUT",
  "NOT_STARTED",
  "ALREADY_STARTED",
  "ABORTED",
  "ICMP_ERROR",
  "TFTP_ERROR",
  "PROTOCOL_ERROR",
  "INCOMPATIBLE_VERSION",
  "SECURITY_VIOLATION",
  "CRC_ERROR",
  "END_OF_MEDIA",
  NULL,
  NULL,
  "END_OF_FILE",
  "INVALID_LANGUAGE",
  "COMPROMISED_DATA",
  "IP_ADDRESS_CONFLICT",
  "HTTP_ERROR",
};

CONST CHAR8 *
StatusDebug (
  IN EFI_STATUS Status,
  OUT CHAR8     Out[BOOT_STATUS_DEBUG_SIZE]
)
{
  CONST UINT64 ErrorBit = (UINT64)1 << 63;
  UINT64       Value    = (UINT64)Status;
  CONST CHAR8 *Name     = NULL;

  if (Value == 0) {
    Name = "SUCCESS";
  } else if (!(Value & ErrorBit) &&
             Value <= sizeof (mWarnings) / sizeof (*mWarnings)) {
    Name = mWarnings[Value - 1];
  } else if ((Value & ErrorBit) && (Value & ~ErrorBit) >= 1 &&
             (Value & ~ErrorBit) <= sizeof (mErrors) / sizeof (*mErrors)) {
    Name = mErrors[(Value & ~ErrorBit) - 1];
  }
  if (Name) {
    AsciiStrCopy (Out, BOOT_STATUS_DEBUG_SIZE, Name);
  } else {
    SPrint (Out, BOOT_STATUS_DEBUG_SIZE, "Status(%llu)", (UINT64)Value);
  }
  return Out;
}

VOID
StringDebugStr (
  IN OUT OABL_STRING *String,
  IN CONST CHAR8     *Text
)
{
  UINTN Length = AsciiStrLen (Text);
  UINTN At     = 0;

  StringAppendChar (String, '"');
  while (At < Length) {
    UINTN  Len       = 1;
    UINT32 CodePoint = Utf8Decode (Text + At, Length - At, &Len);

    switch (CodePoint) {
    case 0:
      StringAppend (String, "\\0");
      break;
    case '\t':
      StringAppend (String, "\\t");
      break;
    case '\r':
      StringAppend (String, "\\r");
      break;
    case '\n':
      StringAppend (String, "\\n");
      break;
    case '\\':
      StringAppend (String, "\\\\");
      break;
    case '"':
      StringAppend (String, "\\\"");
      break;
    default:
      if (CodePoint < 0x20 || (CodePoint >= 0x7f && CodePoint < 0xa0)) {
        StringPrint (String, "\\u{%x}", (UINT32)CodePoint);
      } else {
        StringAppendCodePoint (String, CodePoint);
      }
    }
    At += Len ? Len : 1;
  }
  StringAppendChar (String, '"');
}

VOID
StringDebugOptStr (
  IN OUT OABL_STRING *String,
  IN CONST CHAR8     *Text OPTIONAL
)
{
  if (!Text) {
    StringAppend (String, "None");
    return;
  }
  StringAppend (String, "Some(");
  StringDebugStr (String, Text);
  StringAppendChar (String, ')');
}

VOID
StringDebugStrs (
  IN OUT OABL_STRING *String,
  IN CHAR8 *CONST    *Items,
  IN UINTN            Count
)
{
  StringAppendChar (String, '[');
  for (UINTN Index = 0; Index < Count; Index++) {
    if (Index) {
      StringAppend (String, ", ");
    }
    StringDebugStr (String, Items[Index]);
  }
  StringAppendChar (String, ']');
}

CHAR8 *
DebugStr (IN CONST CHAR8 *Text)
{
  OABL_STRING String = {0};

  StringDebugStr (&String, Text);
  if (String.Failed) {
    StringFree (&String);
    return NULL;
  }
  return StringTake (&String);
}

BOOLEAN
DevicePathValid (
  IN CONST UINT8 *Path,
  IN UINTN        Size
)
{
  UINTN At = 0;

  for (;;) {
    UINT16 Len;

    if (Size - At < 4) {
      return FALSE;
    }
    Len = ReadLe16 (Path + At + 2);
    if (Len < 4 || Len > Size - At) {
      return FALSE;
    }
    if (Path[At] == 0x7F && Path[At + 1] == 0xFF) {
      return TRUE;
    }
    At += Len;
  }
}

UINTN
DevicePathSize (IN CONST VOID *Path OPTIONAL)
{
  CONST UINT8 *Bytes = Path;
  UINTN        At    = 0;

  if (!Bytes) {
    return 0;
  }
  for (;;) {
    UINT16 Len = ReadLe16 (Bytes + At + 2);

    // Give up on a malformed path from the firmware.
    if (Len < 4 || At > 0x10000) {
      return 0;
    }
    if (Bytes[At] == 0x7F && Bytes[At + 1] == 0xFF) {
      return At + Len;
    }
    At += Len;
  }
}

EFI_STATUS
OpenExclusive (
  IN EFI_HANDLE Handle,
  IN EFI_GUID  *Guid,
  OUT VOID    **Out
)
{
  VOID      *Interface = NULL;
  EFI_STATUS Status    = gBS->OpenProtocol (
    Handle, Guid, &Interface, gImageHandle, NULL, EFI_OPEN_PROTOCOL_EXCLUSIVE
  );

  *Out = Status == EFI_SUCCESS ? Interface : NULL;
  return Status;
}

VOID
CloseExclusive (
  IN EFI_HANDLE Handle,
  IN EFI_GUID  *Guid
)
{
  gBS->CloseProtocol (Handle, Guid, gImageHandle, NULL);
}

EFI_STATUS
GetVariable (
  IN CONST CHAR8  *Name,
  IN EFI_GUID     *Vendor,
  OUT OABL_BUFFER *Out
)
{
  UINT16    *WideName = Utf16FromStr (Name);
  UINT8      None     = 0;
  UINTN      Size     = 0;
  UINT32     Attrs    = 0;
  EFI_STATUS Status;

  *Out = (OABL_BUFFER){0};
  if (!WideName) {
    return EFI_OUT_OF_RESOURCES;
  }
  Status = gRT->GetVariable ((CHAR16 *)WideName, Vendor, &Attrs, &Size, &None);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    if (!BufferReserve (Out, Size)) {
      Status = EFI_OUT_OF_RESOURCES;
    } else {
      Status =
        gRT->GetVariable ((CHAR16 *)WideName, Vendor, &Attrs, &Size, Out->Data);
      if (Status == EFI_SUCCESS) {
        Out->Len = Size;
      }
    }
  }
  FreePool (WideName);
  if (Status != EFI_SUCCESS) {
    BufferFree (Out);
  }
  return Status;
}

EFI_STATUS
SetVariable (
  IN CONST CHAR8 *Name,
  IN EFI_GUID    *Vendor,
  IN UINT32       Attributes,
  IN CONST VOID  *Data OPTIONAL,
  IN UINTN        Size
)
{
  UINT16    *WideName = Utf16FromStr (Name);
  UINT8      None     = 0;
  EFI_STATUS Status;

  if (!WideName) {
    return EFI_OUT_OF_RESOURCES;
  }
  // Never pass NULL Data, even for a delete.
  Status = gRT->SetVariable (
    (CHAR16 *)WideName, Vendor, Attributes, Size, Data ? (VOID *)Data : &None
  );
  FreePool (WideName);
  return Status;
}
