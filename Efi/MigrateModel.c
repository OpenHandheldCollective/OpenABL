/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  MigrateModel - finds the device model older loaders stored.
*/

#include "Efi/MigrateModel.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Base/Text.h"
#include "Efi/Qcom.h"

#define STATUS_TEXT(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

// The vendor GUID OpenABL's old settings were stored under:
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

// Before ROCKNIX's LinuxLoader used persistent values, it added fields to
// the end of the struct: RocknixAblVer (64 bytes), is_boot_mode_android and
// is_dev_mode_enabled (1 byte each) and model_selection (64 bytes), which
// made the record 3408 bytes.
#define OFF_OLD_ROCKNIX  DEVINFO_SIZE
#define OLD_ROCKNIX_TEXT 64
#define OLD_ROCKNIX_LEN  (OLD_ROCKNIX_TEXT + 2 + OLD_ROCKNIX_TEXT)
#define OLD_ROCKNIX_SIZE 3408

// Old settings deleted at most, so a firmware whose enumeration never
// ends can't hang the boot.
#define OLD_SETTINGS_MAX 256
// Names GetNextVariableName returns at most per search, likewise.
#define SEARCH_NAMES_MAX 4096

_Static_assert (
  OFF_PERSISTENT + ENTRIES * ENTRY_SIZE <= DEVINFO_SIZE,
  "record"
);
_Static_assert (VALUE_MAX * PIECES <= SETTINGS_MAX_MODEL_LEN, "model");
_Static_assert (OLD_ROCKNIX_TEXT <= SETTINGS_MAX_MODEL_LEN, "old model");
_Static_assert (
  OFF_OLD_ROCKNIX + OLD_ROCKNIX_LEN <= OLD_ROCKNIX_SIZE,
  "ROCKNIX's old fields"
);

STATIC CONST CHAR8 *CONST mPieces[PIECES] = {
  "OablModel0", "OablModel1", "OablModel2"};

// The settings older loaders kept in devinfo's persistent values: ROCKNIX's
// LinuxLoader's, then the ones only OpenABL wrote, among them the SDM845
// builds' copy.
STATIC CONST CHAR8 *CONST mOldSettings[] = {
  "RocknixAblVer", "BootMode",       "DevMode",       "ModelSelection",
  "AltVolumeUp",   "BootSourceMode", "ShowModelMenu", "ForceExternalBoot",
  "OpenAblVer",    "DisplayQuiesce", "OablSettings",  "OablModel0",
  "OablModel1",    "OablModel2"};

#define OLD_SETTINGS (sizeof (mOldSettings) / sizeof (*mOldSettings))

// Reads the old DeviceModel setting. Unread if it is longer than the
// model can be.
STATIC BOOLEAN
OldSettingModel (
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

// TRUE if Field is printable ASCII padded with NULs. An empty field is one.
STATIC BOOLEAN
TextField (IN CONST UINT8 *Field)
{
  UINTN Index = 0;

  while (Index < OLD_ROCKNIX_TEXT && Field[Index] >= 0x20 &&
         Field[Index] < 0x7f) {
    Index++;
  }
  for (; Index < OLD_ROCKNIX_TEXT; Index++) {
    if (Field[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

// TRUE if the bytes after the struct hold the fields ROCKNIX's LinuxLoader
// once added there, as it left them: its version (always written), two
// flags and a model. Anything else may be another ABL's: it is neither
// read nor cleared.
STATIC BOOLEAN
OldRocknixFields (IN CONST UINT8 *Fields)
{
  return Fields[0] != 0 && TextField (Fields) &&
         Fields[OLD_ROCKNIX_TEXT] <= 1 && Fields[OLD_ROCKNIX_TEXT + 1] <= 1 &&
         TextField (Fields + OLD_ROCKNIX_TEXT + 2);
}

// Reads devinfo with ROCKNIX's old fields if VerifiedBoot reads that far,
// else without. *Size is the length read. FALSE if neither read works or
// the record has no magic.
STATIC BOOLEAN
ReadDevinfo (
  OUT OABL_BUFFER *Raw,
  OUT UINTN       *Size
)
{
  UINT64     Revision;
  EFI_STATUS Status = EFI_NOT_FOUND;

  *Size = OLD_ROCKNIX_SIZE;
  if (!QcomReadDeviceState (*Size, &Revision, &Status, Raw) ||
      Status != EFI_SUCCESS) {
    BufferFree (Raw);
    *Size = DEVINFO_SIZE;
    if (!QcomReadDeviceState (*Size, &Revision, &Status, Raw) ||
        Status != EFI_SUCCESS) {
      BufferFree (Raw);
      return FALSE;
    }
  }
  if (Raw->Len < *Size) {
    BufferFree (Raw);
    return FALSE;
  }
  for (UINTN Index = 0; Index < MAGIC_LEN; Index++) {
    if (Raw->Data[Index] != (UINT8)MAGIC[Index]) {
      BufferFree (Raw);
      return FALSE;
    }
  }
  return TRUE;
}

// Finds the model in a devinfo record of Size bytes. The SDM845 copy's
// pieces win over ModelSelection, which wins over ROCKNIX's old
// model_selection field.
STATIC BOOLEAN
RecordModel (
  IN CONST UINT8    *Record,
  IN UINTN           Size,
  OUT UINT8          Out[SETTINGS_MAX_MODEL_LEN],
  OUT UINTN         *Length,
  OUT CONST CHAR8  **From
)
{
  CONST UINT8 *Field = Record + OFF_OLD_ROCKNIX + OLD_ROCKNIX_TEXT + 2;
  OABL_BYTES   Piece;
  BOOLEAN      Pieces = FALSE;

  *Length = 0;
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
  if (*Length == 0 && Size >= OLD_ROCKNIX_SIZE &&
      OldRocknixFields (Record + OFF_OLD_ROCKNIX)) {
    while (*Length < OLD_ROCKNIX_TEXT && Field[*Length]) {
      Out[*Length] = Field[*Length];
      (*Length)++;
    }
    *From = "devinfo's old model_selection field";
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
  UINTN       Size;
  BOOLEAN     Found;

  if (OldSettingModel (Out, Length)) {
    *From = "the old DeviceModel setting";
    return TRUE;
  }
  if (!ReadDevinfo (&Raw, &Size)) {
    return FALSE;
  }
  Found = RecordModel (Raw.Data, Size, Out, Length, From);
  BufferFree (&Raw);
  return Found;
}

// Finds the first old setting under the old vendor GUID, searching from the
// start. Name grows as needed; FreePool it. FALSE if there is none left.
STATIC BOOLEAN
FirstOldSetting (
  IN OUT CHAR16 **Name,
  IN OUT UINTN   *Capacity
)
{
  EFI_GUID   Vendor;
  UINTN      Size;
  EFI_STATUS Status;
  CHAR16    *Bigger;

  if (*Capacity < 64 * sizeof (CHAR16)) {
    Bigger = ResizePool (*Name, 64 * sizeof (CHAR16));
    if (!Bigger) {
      return FALSE;
    }
    *Name     = Bigger;
    *Capacity = 64 * sizeof (CHAR16);
  }
  (*Name)[0] = L'\0';
  for (UINTN Round = 0; Round < SEARCH_NAMES_MAX; Round++) {
    Size   = *Capacity;
    Status = gRT->GetNextVariableName (&Size, *Name, &Vendor);
    if (Status == EFI_BUFFER_TOO_SMALL && Size > *Capacity) {
      // Keeps the name searched from, as realloc does.
      Bigger = ResizePool (*Name, Size);
      if (!Bigger) {
        return FALSE;
      }
      *Name     = Bigger;
      *Capacity = Size;
      continue;
    }
    if (Status != EFI_SUCCESS) {
      return FALSE;
    }
    if (CompareGuid (&Vendor, &mOldVendor)) {
      return TRUE;
    }
  }
  return FALSE;
}

// Deletes every old setting under the old vendor GUID, which only OpenABL
// used, and flushes. Each search starts again from the first one, as
// the order isn't kept once one is deleted.
STATIC VOID
DeleteOldSettings (IN LOG_SINK Log)
{
  CHAR16    *Name     = NULL;
  UINTN      Capacity = 0;
  UINT64     Deleted  = 0;
  UINT8      None     = 0;
  EFI_STATUS Status;

  while (Deleted < OLD_SETTINGS_MAX && FirstOldSetting (&Name, &Capacity)) {
    Status = gRT->SetVariable (Name, &mOldVendor, 0, 0, &None);
    if (Status != EFI_SUCCESS) {
      LogPrint (
        Log, "settings: deleting an old setting: %s",
        STATUS_TEXT (Status)
      );
      break;
    }
    Deleted++;
  }
  FreePool (Name);
  if (!Deleted) {
    return;
  }
  if (QcomFlushVariables (&Status) && Status != EFI_SUCCESS) {
    LogPrint (
      Log, "settings: flushing the deleted settings: %s", STATUS_TEXT (Status)
    );
  }
  LogPrint (Log, "settings: deleted %llu old setting(s)", Deleted);
}

// Zeroes the settings older loaders kept in persistent values. Returns how
// many.
STATIC UINTN
ClearOldSettings (IN OUT UINT8 *Record)
{
  UINTN Cleared = 0;

  for (UINTN Index = 0; Index < ENTRIES; Index++) {
    UINT8    *Entry = Record + OFF_PERSISTENT + Index * ENTRY_SIZE;
    OABL_TEXT Name  = {(CONST CHAR8 *)Entry + 6, ReadLe16 (Entry + 2)};

    if (ReadLe16 (Entry) != 1 || Name.Len > NAME_MAX) {
      continue;
    }
    for (UINTN Old = 0; Old < OLD_SETTINGS; Old++) {
      if (TextEq (Name, TextOf (mOldSettings[Old]))) {
        ZeroMem (Entry, ENTRY_SIZE);
        Cleared++;
        break;
      }
    }
  }
  return Cleared;
}

// Clears what older loaders left in devinfo, in one write.
STATIC VOID
ClearOldDevinfo (IN LOG_SINK Log)
{
  OABL_BUFFER Raw = {0};
  EFI_STATUS  Status;
  UINTN       Size;
  UINTN       Cleared;
  BOOLEAN     Fields = FALSE;

  if (!ReadDevinfo (&Raw, &Size)) {
    return;
  }
  Cleared = ClearOldSettings (Raw.Data);
  if (Size >= OLD_ROCKNIX_SIZE &&
      OldRocknixFields (Raw.Data + OFF_OLD_ROCKNIX)) {
    ZeroMem (Raw.Data + OFF_OLD_ROCKNIX, OLD_ROCKNIX_LEN);
    Fields = TRUE;
  }
  if (!Cleared && !Fields) {
    goto Done;
  }
  if (!QcomWriteDeviceState (Raw.Data, Size, &Status)) {
    goto Done;
  }
  if (Status != EFI_SUCCESS) {
    LogPrint (
      Log, "settings: clearing devinfo's old settings: %s", STATUS_TEXT (Status)
    );
    goto Done;
  }
  LogPrint (
    Log, "settings: cleared %llu old devinfo setting(s)%s", (UINT64)Cleared,
    Fields ? " and ROCKNIX's old devinfo fields" : ""
  );
Done:
  BufferFree (&Raw);
}

VOID
MigrateModelCleanUp (IN LOG_SINK Log)
{
  DeleteOldSettings (Log);
  ClearOldDevinfo (Log);
}
