/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  EfiData - UEFI boot manager data formats as plain bytes.
*/

#include "Core/EfiData.h"

#include "Base/Memory.h"
#include "Base/Print.h"

CONST UINT8 gUefiEndNode[4] = {UEFI_TYPE_END, UEFI_END_ENTIRE, 4, 0};

// Converts little-endian UTF-16 to UTF-8, unpaired surrogates as U+FFFD.
STATIC CHAR8 *
Utf16leText (
  IN CONST UINT8 *Data,
  IN UINTN        Units
)
{
  UINT16 *Utf16 = AllocatePool ((Units ? Units : 1) * sizeof (*Utf16));
  CHAR8  *Text;

  if (!Utf16) {
    return NULL;
  }
  for (UINTN Index = 0; Index < Units; Index++) {
    Utf16[Index] = ReadLe16 (Data + 2 * Index);
  }
  Text = Utf16ToUtf8 (Utf16, Units);
  FreePool (Utf16);
  return Text;
}

// Appends String as NUL-terminated little-endian UTF-16.
STATIC BOOLEAN
PutUtf16z (
  IN OUT OABL_BUFFER *Buffer,
  IN CONST CHAR8     *String
)
{
  UINTN   Units;
  UINT16 *Utf16 = Utf16FromText (TextOf (String), &Units);
  BOOLEAN Ok    = Utf16 != NULL;

  for (UINTN Index = 0; Ok && Index <= Units; Index++) {
    Ok = BufferAppendLe16 (Buffer, Utf16[Index]);
  }
  FreePool (Utf16);
  return Ok;
}

VOID
UefiBootVarName (
  IN UINT16  Number,
  OUT CHAR8 *Out,
  IN UINTN   Size
)
{
  SPrint (Out, Size, "Boot%04X", (UINT32)Number);
}

BOOLEAN
UefiParseU16List (
  IN CONST UINT8    *Bytes,
  IN UINTN           Size,
  OUT UEFI_U16_LIST *Out
)
{
  *Out = (UEFI_U16_LIST){0};
  for (UINTN At = 0; At + 2 <= Size; At += 2) {
    if (!VecPush (Out, ReadLe16 (Bytes + At))) {
      VecFree (Out);
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
UefiU16ListBytes (
  IN CONST UINT16 *Values,
  IN UINTN         Count,
  OUT OABL_BUFFER *Out
)
{
  *Out = (OABL_BUFFER){0};
  for (UINTN Index = 0; Index < Count; Index++) {
    if (!BufferAppendLe16 (Out, Values[Index])) {
      BufferFree (Out);
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
UefiLoadOptionParse (
  IN CONST UINT8       *Bytes,
  IN UINTN              Size,
  OUT UEFI_LOAD_OPTION *Out
)
{
  UINT32 Attributes;
  UINT16 PathLen;
  UINT16 Unit;
  UINTN  At    = 6;
  UINTN  Units = 0;

  *Out = (UEFI_LOAD_OPTION){0};
  if (!GetLe32 (Bytes, Size, 0, &Attributes) ||
      !GetLe16 (Bytes, Size, 4, &PathLen)) {
    return FALSE;
  }
  // The description is NUL-terminated UTF-16.
  for (;;) {
    if (!GetLe16 (Bytes, Size, At, &Unit)) {
      return FALSE;
    }
    At += 2;
    if (Unit == 0) {
      break;
    }
    Units++;
  }
  if (!InBounds (Size, At, PathLen)) {
    return FALSE;
  }
  Out->Attributes  = Attributes;
  Out->Description = Utf16leText (Bytes + 6, Units);
  if (!Out->Description ||
      !BufferFrom (&Out->DevicePath, Bytes + At, PathLen) ||
      !BufferFrom (
        &Out->OptionalData, Bytes + At + PathLen, Size - At - PathLen
      )) {
    UefiLoadOptionFree (Out);
    return FALSE;
  }
  return TRUE;
}

BOOLEAN
UefiLoadOptionToBytes (
  IN CONST UEFI_LOAD_OPTION *Option,
  OUT OABL_BUFFER           *Out
)
{
  *Out = (OABL_BUFFER){0};
  if (BufferAppendLe32 (Out, Option->Attributes) &&
      BufferAppendLe16 (Out, (UINT16)Option->DevicePath.Len) &&
      PutUtf16z (Out, Option->Description) &&
      BufferAppend (Out, Option->DevicePath.Data, Option->DevicePath.Len) &&
      BufferAppend (Out, Option->OptionalData.Data, Option->OptionalData.Len)) {
    return TRUE;
  }
  BufferFree (Out);
  return FALSE;
}

VOID
UefiLoadOptionFree (IN OUT UEFI_LOAD_OPTION *Option)
{
  FreePool (Option->Description);
  BufferFree (&Option->DevicePath);
  BufferFree (&Option->OptionalData);
  *Option = (UEFI_LOAD_OPTION){0};
}

BOOLEAN
UefiNodeEq (
  IN CONST UEFI_NODE *A,
  IN CONST UEFI_NODE *B
)
{
  if (A->Bytes.Len != B->Bytes.Len) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < A->Bytes.Len; Index++) {
    if (A->Bytes.Ptr[Index] != B->Bytes.Ptr[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
UefiNextNode (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size,
  IN OUT UINTN   *At,
  OUT UEFI_NODE  *Out
)
{
  UINTN Offset = *At;
  UINTN Len;

  if (!InBounds (Size, Offset, 4)) {
    return FALSE;
  }
  Len = ReadLe16 (DevicePath + Offset + 2);
  if (DevicePath[Offset] == UEFI_TYPE_END || Len < 4 ||
      !InBounds (Size, Offset, Len)) {
    return FALSE;
  }
  *Out = (UEFI_NODE){
    DevicePath[Offset], DevicePath[Offset + 1], {DevicePath + Offset, Len}};
  *At = Offset + Len;
  return TRUE;
}

BOOLEAN
UefiSameNodes (
  IN CONST UINT8 *A,
  IN UINTN        ASize,
  IN CONST UINT8 *B,
  IN UINTN        BSize
)
{
  UINTN     AtA = 0;
  UINTN     AtB = 0;
  UEFI_NODE NodeA;
  UEFI_NODE NodeB;

  for (;;) {
    BOOLEAN MoreA = UefiNextNode (A, ASize, &AtA, &NodeA);
    BOOLEAN MoreB = UefiNextNode (B, BSize, &AtB, &NodeB);

    if (!MoreA || !MoreB) {
      return MoreA == MoreB;
    }
    if (!UefiNodeEq (&NodeA, &NodeB)) {
      return FALSE;
    }
  }
}

BOOLEAN
UefiJoin (
  IN CONST OABL_BYTES *Parts,
  IN UINTN             Count,
  OUT OABL_BUFFER     *Out
)
{
  UEFI_NODE Node;

  *Out = (OABL_BUFFER){0};
  for (UINTN Index = 0; Index < Count; Index++) {
    for (UINTN At = 0;
         UefiNextNode (Parts[Index].Ptr, Parts[Index].Len, &At, &Node);) {
      if (!BufferAppend (Out, Node.Bytes.Ptr, Node.Bytes.Len)) {
        goto Fail;
      }
    }
  }
  if (BufferAppend (Out, gUefiEndNode, sizeof (gUefiEndNode))) {
    return TRUE;
  }
Fail:
  BufferFree (Out);
  return FALSE;
}

BOOLEAN
UefiFilePathNode (
  IN CONST CHAR8  *Path,
  OUT OABL_BUFFER *Out
)
{
  *Out = (OABL_BUFFER){0};
  // The length is filled in once the text is there.
  if (BufferAppendU8 (Out, UEFI_TYPE_MEDIA) &&
      BufferAppendU8 (Out, UEFI_MEDIA_FILE_PATH) && BufferAppendLe16 (Out, 0) &&
      PutUtf16z (Out, Path)) {
    WriteLe16 (Out->Data + 2, (UINT16)Out->Len);
    return TRUE;
  }
  BufferFree (Out);
  return FALSE;
}

BOOLEAN
UefiPartGuid (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size,
  OUT UINT8       Guid[16]
)
{
  UEFI_NODE  Node;
  OABL_BYTES Hd = {0};

  for (UINTN At = 0; UefiNextNode (DevicePath, Size, &At, &Node);) {
    if (Node.Type == UEFI_TYPE_MEDIA && Node.SubType == UEFI_MEDIA_HARD_DRIVE) {
      Hd = UefiNodeData (&Node);
    }
  }
  // PartitionNumber(4) Start(8) Size(8) Signature(16) MBRType(1)
  // SignatureType(1), 2 for a GUID.
  if (Hd.Len < 38 || Hd.Ptr[37] != 0x02) {
    return FALSE;
  }
  CopyMem (Guid, Hd.Ptr + 20, 16);
  return TRUE;
}

BOOLEAN
UefiPartUuid (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size,
  IN BOOLEAN      Upper,
  OUT CHAR8       Out[UEFI_PART_UUID_SIZE]
)
{
  UINT8 Guid[16];

  Out[0] = 0;
  if (!UefiPartGuid (DevicePath, Size, Guid)) {
    return FALSE;
  }
  SPrint (
    Out, UEFI_PART_UUID_SIZE,
    Upper ? "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X"
          : "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    (UINT32)ReadLe32 (Guid), (UINT32)ReadLe16 (Guid + 4),
    (UINT32)ReadLe16 (Guid + 6), Guid[8], Guid[9], Guid[10], Guid[11], Guid[12],
    Guid[13], Guid[14], Guid[15]
  );
  return TRUE;
}

CHAR8 *
UefiFilePathText (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size
)
{
  OABL_STRING Joined = {0};
  UEFI_NODE   Node;
  BOOLEAN     Any = FALSE;

  for (UINTN At = 0; UefiNextNode (DevicePath, Size, &At, &Node);) {
    OABL_BYTES Data;
    UINTN      Units = 0;
    CHAR8     *Text;

    if (Node.Type != UEFI_TYPE_MEDIA || Node.SubType != UEFI_MEDIA_FILE_PATH) {
      continue;
    }
    Any  = TRUE;
    Data = UefiNodeData (&Node);
    while (Units < Data.Len / 2 && ReadLe16 (Data.Ptr + 2 * Units) != 0) {
      Units++;
    }
    if (Joined.Len && Joined.Str[Joined.Len - 1] != '\\') {
      StringAppendChar (&Joined, '\\');
    }
    Text = Utf16leText (Data.Ptr, Units);
    if (!Text) {
      StringFree (&Joined);
      return NULL;
    }
    StringAppend (&Joined, Text);
    FreePool (Text);
  }
  if (!Any || Joined.Failed) {
    StringFree (&Joined);
    return NULL;
  }
  return StringTake (&Joined);
}

BOOLEAN
UefiStartsWith (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size,
  IN CONST UINT8 *Prefix,
  IN UINTN        PrefixSize
)
{
  UINTN     At       = 0;
  UINTN     PrefixAt = 0;
  UINTN     Count    = 0;
  UEFI_NODE Node;
  UEFI_NODE PrefixNode;

  while (UefiNextNode (Prefix, PrefixSize, &PrefixAt, &PrefixNode)) {
    if (!UefiNextNode (DevicePath, Size, &At, &Node) ||
        !UefiNodeEq (&Node, &PrefixNode)) {
      return FALSE;
    }
    Count++;
  }
  return Count != 0;
}

STATIC BOOLEAN
IsMedia (
  IN CONST UEFI_NODE *Node,
  IN UINT8            SubType
)
{
  return Node->Type == UEFI_TYPE_MEDIA && Node->SubType == SubType;
}

UEFI_PATH_KIND
UefiClassify (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size
)
{
  UEFI_NODE Node;
  UEFI_NODE First   = {0};
  BOOLEAN   Any     = FALSE;
  BOOLEAN   AnyFile = FALSE;

  for (UINTN At = 0; UefiNextNode (DevicePath, Size, &At, &Node);) {
    if (IsMedia (&Node, UEFI_MEDIA_PIWG_FW_FILE) ||
        IsMedia (&Node, UEFI_MEDIA_PIWG_FW_VOL)) {
      return UEFI_PATH_FIRMWARE;
    }
    if (!Any) {
      First = Node;
    }
    Any     = TRUE;
    AnyFile = AnyFile || IsMedia (&Node, UEFI_MEDIA_FILE_PATH);
  }
  if (Any && IsMedia (&First, UEFI_MEDIA_HARD_DRIVE)) {
    return UEFI_PATH_SHORT_FORM_HARD_DRIVE;
  }
  if (Any && IsMedia (&First, UEFI_MEDIA_FILE_PATH)) {
    return UEFI_PATH_SHORT_FORM_FILE;
  }
  return AnyFile ? UEFI_PATH_FULL_FILE : UEFI_PATH_DEVICE_ONLY;
}

CONST CHAR8 *
UefiPathKindName (IN UEFI_PATH_KIND Kind)
{
  switch (Kind) {
  case UEFI_PATH_FIRMWARE:
    return "Firmware";
  case UEFI_PATH_SHORT_FORM_HARD_DRIVE:
    return "ShortFormHardDrive";
  case UEFI_PATH_SHORT_FORM_FILE:
    return "ShortFormFile";
  case UEFI_PATH_FULL_FILE:
    return "FullFile";
  case UEFI_PATH_DEVICE_ONLY:
    return "DeviceOnly";
  }
  return "?";
}

BOOLEAN
UefiSplitFile (
  IN CONST UINT8  *DevicePath,
  IN UINTN         Size,
  OUT OABL_BUFFER *Device,
  OUT OABL_BUFFER *File
)
{
  UEFI_NODE Node;
  BOOLEAN   InFile = FALSE;

  *Device = *File = (OABL_BUFFER){0};
  for (UINTN At = 0; UefiNextNode (DevicePath, Size, &At, &Node);) {
    if (IsMedia (&Node, UEFI_MEDIA_FILE_PATH)) {
      InFile = TRUE;
    }
    if (!BufferAppend (
          InFile ? File : Device, Node.Bytes.Ptr, Node.Bytes.Len
        )) {
      goto Fail;
    }
  }
  if (BufferAppend (Device, gUefiEndNode, sizeof (gUefiEndNode)) &&
      BufferAppend (File, gUefiEndNode, sizeof (gUefiEndNode))) {
    return TRUE;
  }
Fail:
  BufferFree (Device);
  BufferFree (File);
  return FALSE;
}

VOID
UefiTableHeaderCrc (IN OUT UINT8 *Table)
{
  UINT32 Size = ReadLe32 (Table + UEFI_TABLE_HEADER_SIZE);

  WriteLe32 (Table + UEFI_TABLE_HEADER_CRC32, 0);
  WriteLe32 (Table + UEFI_TABLE_HEADER_CRC32, CalculateCrc32 (Table, Size));
}

VOID
UefiRtPropertiesTable (
  IN UINT32 Supported,
  OUT UINT8 Out[8]
)
{
  WriteLe16 (Out, 1);
  WriteLe16 (Out + 2, 8);
  WriteLe32 (Out + 4, Supported);
}

VOID
UefiEsrtOne (
  IN CONST UINT8 FwClass[16],
  IN UINT32      Type,
  IN UINT32      Version,
  OUT UINT8      Out[UEFI_ESRT_ONE_SIZE]
)
{
  // FwResourceCount, FwResourceCountMax, FwResourceVersion.
  WriteLe32 (Out, 1);
  WriteLe32 (Out + 4, 1);
  WriteLe64 (Out + 8, 1);
  CopyMem (Out + 16, FwClass, 16);
  WriteLe32 (Out + 32, Type);
  WriteLe32 (Out + 36, Version);
  // LowestSupportedFwVersion, CapsuleFlags.
  WriteLe32 (Out + 40, 0);
  WriteLe32 (Out + 44, 0);
  // LastAttemptVersion and LastAttemptStatus (success).
  WriteLe32 (Out + 48, Version);
  WriteLe32 (Out + 52, 0);
}

UINT32
UefiVersionPair (IN CONST CHAR8 *Version)
{
  UINT32 Part[2] = {0, 0};
  UINTN  Index   = 0;

  if (!Version || *Version < '0' || *Version > '9') {
    return 0;
  }
  for (; *Version && Index < 2; Version++) {
    if (*Version >= '0' && *Version <= '9') {
      Part[Index] = Part[Index] * 10 + (UINT32)(*Version - '0');
    } else if (*Version == '.') {
      Index++;
    } else {
      break;
    }
  }
  return (Part[0] & 0xFFFF) << 16 | (Part[1] & 0xFFFF);
}

BOOLEAN
UefiParseRtProperties (
  IN CONST UINT8 *Table,
  IN UINTN        Size,
  OUT UINT16     *Version,
  OUT UINT32     *Supported
)
{
  UINT16 TableVersion;
  UINT32 Mask;

  if (!GetLe16 (Table, Size, 0, &TableVersion) ||
      !GetLe32 (Table, Size, 4, &Mask)) {
    return FALSE;
  }
  *Version   = TableVersion;
  *Supported = Mask;
  return TRUE;
}

BOOLEAN
UefiPaddedDtb (
  IN CONST UINT8  *Blob,
  IN UINTN         Size,
  IN UINTN         Pad,
  OUT OABL_BUFFER *Out
)
{
  UINT32 Total;

  *Out = (OABL_BUFFER){0};
  if (!GetBe32 (Blob, Size, 4, &Total)) {
    return FALSE;
  }
  if (ReadBe32 (Blob) != 0xd00dfeed || Total > Size) {
    return FALSE;
  }
  // The copy must hold the totalsize field and must not overflow.
  if (Pad > MAX_UINTN - Total || Total + Pad < 8) {
    return FALSE;
  }
  if (!BufferReserve (Out, Total + Pad) || !BufferAppend (Out, Blob, Total) ||
      !BufferAppendZeros (Out, Pad)) {
    BufferFree (Out);
    return FALSE;
  }
  WriteBe32 (Out->Data + 4, (UINT32)(Total + Pad));
  return TRUE;
}
