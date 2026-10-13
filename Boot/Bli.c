/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Bli - systemd's Boot Loader Interface variables.
*/

#include "Boot/Bli.h"

#include "Base/Memory.h"
#include "Base/Buffer.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Boot/BootUtil.h"
#include "Efi/Efi.h"
#include "Efi/Serial.h"

EFI_GUID gBliVendor = {0x4a67b082,
                       0x0a4c,
                       0x41cf,
                       {0xb6, 0xc7, 0x44, 0x0b, 0x29, 0xbb, 0x8c, 0x4f}};

STATIC CONST UINT64 mFeatures =
  BLI_FEATURE_CONFIG_TIMEOUT_ONE_SHOT | BLI_FEATURE_ENTRY_DEFAULT |
  BLI_FEATURE_ENTRY_ONESHOT | BLI_FEATURE_BOOT_COUNTING | BLI_FEATURE_XBOOTLDR |
  BLI_FEATURE_SORT_KEY | BLI_FEATURE_DEVICETREE;

STATIC UINT32
VolatileAttrs (VOID)
{
  return EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;
}

// Appends Text to Out as UTF-16 bytes with a terminator.
STATIC BOOLEAN
Utf16 (
  IN OUT OABL_BUFFER *Out,
  IN CONST CHAR8     *Text
)
{
  UINTN   Units = 0;
  UINT16 *Wide  = Utf16FromText (TextOf (Text), &Units);
  BOOLEAN Ok    = FALSE;

  if (Wide && BufferReserve (Out, (Units + 1) * 2)) {
    for (UINTN Index = 0; Index <= Units; Index++) {
      BufferAppendLe16 (Out, Wide[Index]);
    }
    Ok = TRUE;
  }
  FreePool (Wide);
  return Ok;
}

// Decodes little-endian UTF-16 up to its terminator into new UTF-8 text.
// Returns NULL when out of memory.
STATIC CHAR8 *
Decode (
  IN CONST UINT8 *Data,
  IN UINTN        Size
)
{
  UINTN   Units = 0;
  UINT16 *Wide;
  CHAR8  *Text;

  while (Units < Size / 2 && ReadLe16 (Data + 2 * Units)) {
    Units++;
  }
  Wide = AllocatePool ((Units ? Units : 1) * 2);
  if (!Wide) {
    return NULL;
  }
  for (UINTN Index = 0; Index < Units; Index++) {
    Wide[Index] = ReadLe16 (Data + 2 * Index);
  }
  Text = Utf16ToUtf8 (Wide, Units);
  FreePool (Wide);
  return Text;
}

CHAR8 *
BliGet (IN CONST CHAR8 *Name)
{
  OABL_BUFFER Data;
  CHAR8      *Text;

  if (GetVariable (Name, &gBliVendor, &Data) != EFI_SUCCESS) {
    return NULL;
  }
  Text = Decode (Data.Data, Data.Len);
  BufferFree (&Data);
  return Text;
}

CHAR8 *
BliTake (IN CONST CHAR8 *Name)
{
  CHAR8 *Value = BliGet (Name);
  CHAR8 *Quoted;

  if (!Value) {
    return NULL;
  }
  DeleteVariable (Name, &gBliVendor);
  Quoted = DebugStr (Value);
  SerialLog ("%s = %s (one-shot, cleared)", Name, Quoted);
  FreePool (Quoted);
  return Value;
}

STATIC VOID
SetRaw (
  IN CONST CHAR8 *Name,
  IN CONST VOID  *Data,
  IN UINTN        Size
)
{
  EFI_STATUS Status =
    SetVariable (Name, &gBliVendor, VolatileAttrs (), Data, Size);

  if (Status != EFI_SUCCESS) {
    SerialLog (
      "SetVariable %s failed: %s", Name,
      StatusDebug (Status, (CHAR8[BOOT_STATUS_DEBUG_SIZE]){0})
    );
  }
}

VOID
BliSet (
  IN CONST CHAR8 *Name,
  IN CONST CHAR8 *Value
)
{
  OABL_BUFFER Data = {0};

  if (Utf16 (&Data, Value)) {
    SetRaw (Name, Data.Data, Data.Len);
  }
  BufferFree (&Data);
}

// Like BliSet, with a printf-style format.
STATIC VOID
SetFmt (
  IN CONST CHAR8 *Name,
  IN CONST CHAR8 *Format,
  ...
)
  __attribute__ ((format (
    printf,
    2,
    3
  )));
STATIC VOID
SetFmt (
  IN CONST CHAR8 *Name,
  IN CONST CHAR8 *Format,
  ...
)
{
  va_list Ap;
  CHAR8  *Value;

  va_start (Ap, Format);
  Value = VSPrintAlloc (Format, Ap);
  va_end (Ap);
  if (Value) {
    BliSet (Name, Value);
  }
  FreePool (Value);
}

VOID
BliReport (IN CONST BLI_STARTED *Started)
{
  OABL_BUFFER List = {0};
  OABL_STRING Line = {0};
  UINT8       Features[8];
  UINT32      Firmware = gST->FirmwareRevision;
  UINT32      Uefi     = gST->Hdr.Revision;
  CHAR8      *Vendor;
  BOOLEAN     Ok = TRUE;

  SetFmt ("LoaderTimeInitUSec", "%llu", (UINT64)Started->InitUs);
  BliSet ("LoaderInfo", Started->LoaderInfo);
  Vendor = gST->FirmwareVendor
             ? Utf16ToStr ((CONST UINT16 *)gST->FirmwareVendor)
             : AsciiStrDup ("");
  if (Vendor) {
    SetFmt (
      "LoaderFirmwareInfo", "%s %u.%02u", Vendor, (UINT32)(Firmware >> 16),
      (UINT32)(Firmware & 0xffff)
    );
  }
  FreePool (Vendor);
  SetFmt (
    "LoaderFirmwareType", "UEFI %u.%02u", (UINT32)(UINT16)(Uefi >> 16),
    (UINT32)(UINT16)Uefi
  );
  WriteLe64 (Features, mFeatures);
  SetRaw ("LoaderFeatures", Features, sizeof (Features));
  if (Started->ImagePath) {
    BliSet ("LoaderImageIdentifier", Started->ImagePath);
  }
  if (Started->PartUuid) {
    BliSet ("LoaderDevicePartUUID", Started->PartUuid);
  }
  for (UINTN Index = 0; Index < Started->EntryCount && Ok; Index++) {
    Ok = Utf16 (&List, Started->Entries[Index]);
  }
  if (Ok) {
    SetRaw ("LoaderEntries", List.Data, List.Len);
  }
  BufferFree (&List);
  BliSet ("LoaderEntrySelected", Started->Selected);
  if (Started->CountPath) {
    BliSet ("LoaderBootCountPath", Started->CountPath);
  }
  SetFmt ("LoaderTimeExecUSec", "%llu", (UINT64)ClockNowUs ());

  StringPrint (
    &Line, "Boot Loader Interface: selected %s, partition ", Started->Selected
  );
  StringDebugOptStr (&Line, Started->PartUuid);
  StringAppend (&Line, ", count path ");
  StringDebugOptStr (&Line, Started->CountPath);
  StringPrint (&Line, ", %zu entries", Started->EntryCount);
  SerialLog ("%s", StringGet (&Line));
  StringFree (&Line);
}
