/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DevInfo - LinuxLoader's devinfo record, read to carry user settings over.
*/

#include "Core/DevInfo.h"

#include "Base/Buffer.h"

// Offsets in the record.
#define OFF_UNLOCKED           13
#define OFF_UNLOCK_CRITICAL    14
#define OFF_CHARGER_SCREEN     15
#define OFF_BOOTLOADER_VERSION 16
// After the bootloader and radio versions (64 bytes each).
#define OFF_VERITY_MODE 144
#define OFF_PERSISTENT  2456
// A persistent value entry: in-use flag, name size and value size (UINT16
// each), then the name (56 bytes) and the value (32 bytes).
#define ENTRY_SIZE 94

// A legacy model converted to UTF-8 must fit SETTINGS.Model.
_Static_assert (
  DEVINFO_VALUE_MAX * 3 + 1 <= sizeof ((SETTINGS *)0)->Model,
  "model too small"
);

CONST DEVINFO_LEGACY gDevInfoLegacySettings[DEVINFO_LEGACY_SETTINGS_LEN] = {
  {SETTINGS_KEY_BOOT_MODE, "BootMode"},
  {SETTINGS_KEY_ABL_MODE, "DevMode"},
  {SETTINGS_KEY_ALT_VOLUME_UP, "AltVolumeUp"},
  {SETTINGS_KEY_BOOT_SOURCE, "BootSourceMode"},
  {SETTINGS_KEY_MODEL, "ModelSelection"},
  {SETTINGS_KEY_SHOW_MODEL_MENU, "ShowModelMenu"},
  {SETTINGS_KEY_DISPLAY_QUIESCE, "DisplayQuiesce"},
};

CONST CHAR8 *CONST gDevInfoLegacyOnly[DEVINFO_LEGACY_ONLY_LEN] = {
  "OpenAblVer", "ForceExternalBoot"};

STATIC BOOLEAN
IsRecord (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size
)
{
  if (Size < DEVINFO_SIZE) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < DEVINFO_MAGIC_LEN; Index++) {
    if (Bytes[Index] != (UINT8)DEVINFO_MAGIC[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

// Copies Bytes into Out as UTF-8, replacing invalid sequences with U+FFFD,
// and returns the length. Out needs room for 3 * Size + 1 bytes.
STATIC UINTN
Lossy (
  OUT CHAR8      *Out,
  IN CONST UINT8 *Bytes,
  IN UINTN        Size
)
{
  UINTN Length = 0;
  UINTN SequenceLength;

  for (UINTN Index = 0; Index < Size; Index += SequenceLength) {
    if (Utf8Decode (
          (CONST CHAR8 *)Bytes + Index, Size - Index, &SequenceLength
        ) == 0xfffd) {
      Out[Length++] = (CHAR8)0xef;
      Out[Length++] = (CHAR8)0xbf;
      Out[Length++] = (CHAR8)0xbd;
    } else {
      for (UINTN Index2 = 0; Index2 < SequenceLength; Index2++) {
        Out[Length++] = (CHAR8)Bytes[Index + Index2];
      }
    }
  }
  Out[Length] = 0;
  return Length;
}

UINTN
DevInfoSettingsMove (
  IN CONST DEVINFO *Info OPTIONAL,
  OUT SETTINGS_VAR  Out[DEVINFO_SETTINGS_MOVE_LEN]
)
{
  OABL_BYTES Value;
  UINTN      Count = 0;
  BOOLEAN    Charger;

  for (UINTN Index = 0; Index < DEVINFO_LEGACY_SETTINGS_LEN; Index++, Count++) {
    CONST CHAR8 *Key = gDevInfoLegacySettings[Index].Key;

    Out[Count].Name = Key;
    if (!Info ||
        !DevInfoLegacySetting (Info, Key, Out[Count].Value, &Out[Count].Len)) {
      Out[Count].Len = 0;
    }
  }
  Charger =
    !Info || Info->ChargerScreen || !DevInfoValue (Info, "OpenAblVer", &Value);
  Out[Count++] = (SETTINGS_VAR){
    .Name = SETTINGS_KEY_OFFMODE_CHARGING, .Value = {Charger}, .Len = 1};
  Out[Count++] = (SETTINGS_VAR){
    .Name = SETTINGS_KEY_SCHEMA, .Value = {SETTINGS_SCHEMA}, .Len = 1};
  return Count;
}

BOOLEAN
DevInfoSetChargerScreen (
  IN OUT UINT8 *Raw,
  IN UINTN      Size,
  IN BOOLEAN    On,
  OUT BOOLEAN  *Changed
)
{
  UINT8 Old;

  if (!IsRecord (Raw, Size)) {
    return FALSE;
  }
  Old                     = Raw[OFF_CHARGER_SCREEN];
  Raw[OFF_CHARGER_SCREEN] = On;
  *Changed                = Old != On;
  return TRUE;
}

BOOLEAN
DevInfoClearValues (
  IN OUT UINT8          *Raw,
  IN UINTN               Size,
  IN CONST CHAR8 *CONST *Names,
  IN UINTN               NameCount
)
{
  BOOLEAN Changed = FALSE;

  if (Size < DEVINFO_SIZE) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < DEVINFO_ENTRIES; Index++) {
    UINT8    *Entry = Raw + OFF_PERSISTENT + Index * ENTRY_SIZE;
    OABL_TEXT Name  = {(CONST CHAR8 *)Entry + 6, ReadLe16 (Entry + 2)};

    if (ReadLe16 (Entry) != 1 || Name.Len > DEVINFO_NAME_MAX) {
      continue;
    }
    for (UINTN NameIndex = 0; NameIndex < NameCount; NameIndex++) {
      if (TextEq (Name, TextOf (Names[NameIndex]))) {
        for (UINTN Byte = 0; Byte < ENTRY_SIZE; Byte++) {
          Entry[Byte] = 0;
        }
        Changed = TRUE;
        break;
      }
    }
  }
  return Changed;
}

BOOLEAN
DevInfoSetValue (
  IN OUT UINT8   *Raw,
  IN UINTN        Size,
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Value,
  IN UINTN        ValueSize
)
{
  OABL_TEXT Wanted = TextOf (Name);
  UINT8    *Entry  = NULL;

  if (Size < DEVINFO_SIZE || Wanted.Len == 0 || Wanted.Len > DEVINFO_NAME_MAX ||
      ValueSize > DEVINFO_VALUE_MAX) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < DEVINFO_ENTRIES && !Entry; Index++) {
    UINT8 *Candidate = Raw + OFF_PERSISTENT + Index * ENTRY_SIZE;
    UINTN  NameSize  = ReadLe16 (Candidate + 2);

    if (ReadLe16 (Candidate) == 1 && NameSize == Wanted.Len &&
        TextEq ((OABL_TEXT){(CONST CHAR8 *)Candidate + 6, NameSize}, Wanted)) {
      Entry = Candidate;
    }
  }
  for (UINTN Index = 0; Index < DEVINFO_ENTRIES && !Entry; Index++) {
    UINT8 *Candidate = Raw + OFF_PERSISTENT + Index * ENTRY_SIZE;

    if (ReadLe16 (Candidate) != 1) {
      Entry = Candidate;
    }
  }
  if (!Entry) {
    return FALSE;
  }
  for (UINTN Byte = 0; Byte < ENTRY_SIZE; Byte++) {
    Entry[Byte] = 0;
  }
  WriteLe16 (Entry, 1);
  WriteLe16 (Entry + 2, (UINT16)Wanted.Len);
  WriteLe16 (Entry + 4, (UINT16)ValueSize);
  for (UINTN Byte = 0; Byte < Wanted.Len; Byte++) {
    Entry[6 + Byte] = (UINT8)Wanted.Ptr[Byte];
  }
  for (UINTN Byte = 0; Byte < ValueSize; Byte++) {
    Entry[6 + DEVINFO_NAME_MAX + Byte] = Value[Byte];
  }
  return TRUE;
}

BOOLEAN
DevInfoLegacySetting (
  IN CONST DEVINFO *Info,
  IN CONST CHAR8   *Key,
  OUT UINT8        *Out,
  OUT UINTN        *Length
)
{
  CONST CHAR8 *Name = NULL;
  OABL_BYTES   Value;
  UINTN        Count;

  for (UINTN Index = 0; Index < DEVINFO_LEGACY_SETTINGS_LEN && !Name; Index++) {
    if (AsciiStrEq (gDevInfoLegacySettings[Index].Key, Key)) {
      Name = gDevInfoLegacySettings[Index].Name;
    }
  }
  if (!Name || !DevInfoValue (Info, Name, &Value)) {
    return FALSE;
  }
  for (Count = 0; Count < Value.Len; Count++) {
    Out[Count] = Value.Ptr[Count];
  }
  if (AsciiStrEq (Key, SETTINGS_KEY_MODEL)) {
    for (Count = 0; Count < Value.Len && Out[Count]; Count++)
      ;
  } else if (AsciiStrEq (Key, SETTINGS_KEY_BOOT_SOURCE) && Count == 1 &&
             Out[0] <= 3) {
    Out[0] ^= 1;
  }
  *Length = Count;
  return Count != 0;
}

BOOLEAN
DevInfoParse (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  OUT DEVINFO    *Out
)
{
  CONST UINT8 *Version = Bytes + OFF_BOOTLOADER_VERSION;
  UINTN        End     = 0;

  if (!IsRecord (Bytes, Size)) {
    return FALSE;
  }
  *Out = (DEVINFO){
    .Unlocked       = Bytes[OFF_UNLOCKED] != 0,
    .UnlockCritical = Bytes[OFF_UNLOCK_CRITICAL] != 0,
    .ChargerScreen  = Bytes[OFF_CHARGER_SCREEN] != 0,
  };
  while (End < 64 && Version[End]) {
    End++;
  }
  Lossy (Out->BootloaderVersion, Version, End);
  for (UINTN Index = 0; Index < DEVINFO_ENTRIES; Index++) {
    CONST UINT8   *Entry     = Bytes + OFF_PERSISTENT + Index * ENTRY_SIZE;
    UINTN          InUse     = ReadLe16 (Entry);
    UINTN          NameSize  = ReadLe16 (Entry + 2);
    UINTN          ValueSize = ReadLe16 (Entry + 4);
    DEVINFO_VALUE *Value;

    if (InUse != 1 || NameSize == 0 || NameSize > DEVINFO_NAME_MAX ||
        ValueSize > DEVINFO_VALUE_MAX) {
      continue;
    }
    Value          = &Out->Values[Out->ValueCount++];
    Value->NameLen = Lossy (Value->Name, Entry + 6, NameSize);
    for (UINTN Byte = 0; Byte < ValueSize; Byte++) {
      Value->Value[Byte] = Entry[6 + DEVINFO_NAME_MAX + Byte];
    }
    Value->ValueLen = ValueSize;
  }
  return TRUE;
}

BOOLEAN
DevInfoValue (
  IN CONST DEVINFO *Info,
  IN CONST CHAR8   *Name,
  OUT OABL_BYTES   *Value
)
{
  for (UINTN Index = 0; Index < Info->ValueCount; Index++) {
    CONST DEVINFO_VALUE *Entry = &Info->Values[Index];

    if (TextEq ((OABL_TEXT){Entry->Name, Entry->NameLen}, TextOf (Name))) {
      *Value = BytesOf (Entry->Value, Entry->ValueLen);
      return TRUE;
    }
  }
  return FALSE;
}

VOID
DevInfoToBytes (
  IN CONST DEVINFO *Info,
  OUT UINT8         Out[DEVINFO_SIZE]
)
{
  UINTN VersionLength = AsciiStrLen (Info->BootloaderVersion);

  for (UINTN Index = 0; Index < DEVINFO_SIZE; Index++) {
    Out[Index] = 0;
  }
  for (UINTN Index = 0; Index < DEVINFO_MAGIC_LEN; Index++) {
    Out[Index] = (UINT8)DEVINFO_MAGIC[Index];
  }
  Out[OFF_UNLOCKED]        = Info->Unlocked;
  Out[OFF_UNLOCK_CRITICAL] = Info->UnlockCritical;
  Out[OFF_CHARGER_SCREEN]  = Info->ChargerScreen;
  // dm-verity enforcing, as the ABL sets it on a new record. Android's
  // dm-verity reports clear it (logging), and Qualcomm's ABL then shows its
  // 30 s EIO warning and powers off.
  Out[OFF_VERITY_MODE] = 1;
  for (UINTN Index = 0; Index < VersionLength && Index < 63; Index++) {
    Out[OFF_BOOTLOADER_VERSION + Index] = (UINT8)Info->BootloaderVersion[Index];
  }
  for (UINTN Index = 0; Index < Info->ValueCount && Index < DEVINFO_ENTRIES;
       Index++) {
    CONST DEVINFO_VALUE *Value = &Info->Values[Index];
    UINT8               *Entry = Out + OFF_PERSISTENT + Index * ENTRY_SIZE;
    UINTN                NameSize =
      Value->NameLen < DEVINFO_NAME_MAX ? Value->NameLen : DEVINFO_NAME_MAX;
    UINTN ValueSize =
      Value->ValueLen < DEVINFO_VALUE_MAX ? Value->ValueLen : DEVINFO_VALUE_MAX;

    WriteLe16 (Entry, 1);
    WriteLe16 (Entry + 2, (UINT16)NameSize);
    WriteLe16 (Entry + 4, (UINT16)ValueSize);
    for (UINTN Byte = 0; Byte < NameSize; Byte++) {
      Entry[6 + Byte] = (UINT8)Value->Name[Byte];
    }
    for (UINTN Byte = 0; Byte < ValueSize; Byte++) {
      Entry[6 + DEVINFO_NAME_MAX + Byte] = Value->Value[Byte];
    }
  }
}

BOOLEAN
DevInfoHasLegacySettings (IN CONST DEVINFO *Info)
{
  STATIC CONST CHAR8 *CONST Names[] = {"BootMode", "DevMode", "AltVolumeUp",
                                       "BootSourceMode", "ModelSelection"};
  OABL_BYTES                Value;

  for (UINTN Index = 0; Index < sizeof (Names) / sizeof (*Names); Index++) {
    if (DevInfoValue (Info, Names[Index], &Value)) {
      return TRUE;
    }
  }
  return FALSE;
}

// Fails if persistent value Name is missing or empty.
STATIC BOOLEAN
FirstByte (
  IN CONST DEVINFO *Info,
  IN CONST CHAR8   *Name,
  OUT UINT8        *Byte
)
{
  OABL_BYTES Value;

  if (!DevInfoValue (Info, Name, &Value) || !Value.Len) {
    return FALSE;
  }
  *Byte = Value.Ptr[0];
  return TRUE;
}

VOID
DevInfoApplyOpenAblSettings (
  IN CONST DEVINFO *Info,
  IN OUT SETTINGS  *Settings
)
{
  OABL_BYTES Value;
  UINT8      Byte;

  if (FirstByte (Info, "BootMode", &Byte)) {
    Settings->BootMode = Byte ? BOOT_MODE_ANDROID : BOOT_MODE_LINUX;
  }
  if (FirstByte (Info, "DevMode", &Byte)) {
    Settings->AblMode = Byte ? ABL_MODE_VERBOSE : ABL_MODE_SILENT;
  }
  if (FirstByte (Info, "AltVolumeUp", &Byte)) {
    Settings->AltVolumeUp = Byte != 0;
  }
  if (FirstByte (Info, "BootSourceMode", &Byte)) {
    switch (Byte) {
    case 0:
      Settings->BootSource = BOOT_SOURCE_INTERNAL;
      break;
    case 2:
      Settings->BootSource = BOOT_SOURCE_USB;
      break;
    case 3:
      Settings->BootSource = BOOT_SOURCE_SD;
      break;
    default:
      Settings->BootSource = BOOT_SOURCE_AUTO;
      break;
    }
  }
  // The model is cut at its NUL. Empty means no model.
  if (DevInfoValue (Info, "ModelSelection", &Value)) {
    UINTN End = 0;

    while (End < Value.Len && Value.Ptr[End]) {
      End++;
    }
    Lossy (Settings->Model, Value.Ptr, End);
  }
  Settings->OffmodeCharging = Info->ChargerScreen;
}
