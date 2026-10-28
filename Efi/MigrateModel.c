/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  MigrateModel - finds the device model older loaders stored.
*/

#include "Efi/MigrateModel.h"

#include "Base/Buffer.h"
#include "Base/Text.h"
#include "Efi/Qcom.h"

// The vendor GUID of the old settings variables:
// 8c5f2b7e-3a41-4d6c-9e2f-5b7a1c0d4e93.
STATIC EFI_GUID mOldVendor = {
  0x8c5f2b7e, 0x3a41, 0x4d6c, {0x9e, 0x2f, 0x5b, 0x7a, 0x1c, 0x0d, 0x4e, 0x93}
};

// The devinfo record, as LinuxLoader's DeviceInfo.h lays it out.
#define DEVINFO_SIZE 3272
// Not NUL-terminated in the record.
#define MAGIC     "ANDROID-BOOT!"
#define MAGIC_LEN 13
// The persistent values: an in-use flag, name size and value size (UINT16
// each), then the name (56 bytes) and the value (32 bytes).
#define OFF_PERSISTENT 2456
#define ENTRIES        8
#define ENTRY_SIZE     94
#define NAME_MAX       56
#define VALUE_MAX      32

#define PIECES 3

_Static_assert (
  OFF_PERSISTENT + ENTRIES * ENTRY_SIZE <= DEVINFO_SIZE,
  "record"
);
_Static_assert (VALUE_MAX * PIECES <= SETTINGS_MAX_MODEL_LEN, "model");

STATIC CONST CHAR8 *CONST mPieces[PIECES] = {
  "OablModel0", "OablModel1", "OablModel2"};

// Reads the old DeviceModel variable. Unread if it is longer than the
// model can be.
STATIC BOOLEAN
VariableModel (
  OUT UINT8  Out[SETTINGS_MAX_MODEL_LEN],
  OUT UINTN *Length
)
{
  STATIC CHAR16 Name[] = L"DeviceModel";
  UINTN         Size   = SETTINGS_MAX_MODEL_LEN;

  if (gRT->GetVariable (Name, &mOldVendor, NULL, &Size, Out) != EFI_SUCCESS) {
    return FALSE;
  }
  *Length = Size;
  return Size != 0;
}

// Finds persistent value Name in a record.
STATIC BOOLEAN
Value (
  IN CONST UINT8 *Record,
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Out
)
{
  OABL_TEXT Wanted = TextOf (Name);

  for (UINTN Index = 0; Index < ENTRIES; Index++) {
    CONST UINT8 *Entry     = Record + OFF_PERSISTENT + Index * ENTRY_SIZE;
    UINTN        NameSize  = ReadLe16 (Entry + 2);
    UINTN        ValueSize = ReadLe16 (Entry + 4);

    if (ReadLe16 (Entry) == 1 && NameSize <= NAME_MAX &&
        ValueSize <= VALUE_MAX &&
        TextEq ((OABL_TEXT){(CONST CHAR8 *)Entry + 6, NameSize}, Wanted)) {
      *Out = BytesOf (Entry + 6 + NAME_MAX, ValueSize);
      return TRUE;
    }
  }
  return FALSE;
}

// Finds the model in a devinfo record. The SDM845 copy's pieces win over
// ModelSelection.
STATIC BOOLEAN
RecordModel (
  IN CONST UINT8    *Record,
  OUT UINT8          Out[SETTINGS_MAX_MODEL_LEN],
  OUT UINTN         *Length,
  OUT CONST CHAR8  **From
)
{
  OABL_BYTES Piece;
  BOOLEAN    Pieces = FALSE;

  *Length = 0;
  for (UINTN Index = 0; Index < MAGIC_LEN; Index++) {
    if (Record[Index] != (UINT8)MAGIC[Index]) {
      return FALSE;
    }
  }
  for (UINTN Index = 0; Index < PIECES; Index++) {
    if (!Value (Record, mPieces[Index], &Piece)) {
      continue;
    }
    Pieces = TRUE;
    for (UINTN Byte = 0; Byte < Piece.Len; Byte++) {
      Out[(*Length)++] = Piece.Ptr[Byte];
    }
  }
  *From = "devinfo's OablModel values";
  if (!Pieces && Value (Record, "ModelSelection", &Piece)) {
    while (*Length < Piece.Len && Piece.Ptr[*Length]) {
      Out[*Length] = Piece.Ptr[*Length];
      (*Length)++;
    }
    *From = "devinfo's ModelSelection";
  }
  return *Length != 0;
}

BOOLEAN
MigrateModel (
  OUT UINT8         Out[SETTINGS_MAX_MODEL_LEN],
  OUT UINTN        *Length,
  OUT CONST CHAR8 **From
)
{
  OABL_BUFFER Raw = {0};
  UINT64      Revision;
  EFI_STATUS  Status;
  BOOLEAN     Found;

  if (VariableModel (Out, Length)) {
    *From = "the DeviceModel UEFI variable";
    return TRUE;
  }
  Found = QcomReadDeviceState (DEVINFO_SIZE, &Revision, &Status, &Raw) &&
          Status == EFI_SUCCESS && Raw.Len >= DEVINFO_SIZE &&
          RecordModel (Raw.Data, Out, Length, From);
  BufferFree (&Raw);
  return Found;
}
