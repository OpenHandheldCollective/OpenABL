/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gpt - GPT parsing and rewriting for installing and uninstalling
  PortareOS on internal storage.
*/

#include "Core/Gpt.h"

#include "Base/Memory.h"

CONST UINT8 gGptLinuxFs[16] = {0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
                               0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4};
CONST UINT8 gGptEfiSystem[16] = {0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8,
                                 0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0,
                                 0xc9, 0x3e, 0xc9, 0x3b};

STATIC CONST CHAR8 mUserdata[] = "userdata";
// Entry name: 36 UTF-16 code units at byte 56.
#define NAME_AT   56
#define NAME_LEN  72
#define MIN_ENTRY 128
// Well above any real table; 128 entries of 128 bytes take 16 KiB.
#define MAX_ARRAY ((UINT64)1 << 20)

STATIC BOOLEAN
BytesEq (
  IN CONST UINT8 *A,
  IN CONST UINT8 *B,
  IN UINTN        Size
)
{
  for (UINTN Index = 0; Index < Size; Index++) {
    if (A[Index] != B[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
GptParse (
  IN CONST UINT8 *Block,
  IN UINTN        Len,
  OUT GPT_HEADER *Out
)
{
  GPT_HEADER Header;

  if (Len < 92 || !BytesEq (Block, (CONST UINT8 *)GPT_SIGNATURE, 8)) {
    return FALSE;
  }
  Header.HeaderSize   = ReadLe32 (Block + 12);
  Header.MyLba        = ReadLe64 (Block + 24);
  Header.AlternateLba = ReadLe64 (Block + 32);
  Header.FirstUsable  = ReadLe64 (Block + 40);
  Header.LastUsable   = ReadLe64 (Block + 48);
  Header.EntryLba     = ReadLe64 (Block + 72);
  Header.Entries      = ReadLe32 (Block + 80);
  Header.EntrySize    = ReadLe32 (Block + 84);
  if (Header.HeaderSize < 92 || Header.HeaderSize > Len ||
      Header.EntrySize < MIN_ENTRY || Header.EntrySize % 8 ||
      (UINT64)Header.Entries * Header.EntrySize > MAX_ARRAY) {
    return FALSE;
  }
  *Out = Header;
  return TRUE;
}

UINTN
GptArraySize (IN CONST GPT_HEADER *Header)
{
  return (UINTN)Header->Entries * Header->EntrySize;
}

// Number of whole entries in ArrayLen bytes, at most the header's count.
// Entries under 128 bytes are too short for the fields read, so 0.
STATIC UINTN
EntryCount (
  IN CONST GPT_HEADER *Header,
  IN UINTN             ArrayLen
)
{
  UINTN Count;

  if (Header->EntrySize < MIN_ENTRY) {
    return 0;
  }
  Count = ArrayLen / Header->EntrySize;
  return Count < Header->Entries ? Count : Header->Entries;
}

// An unused entry has an all-zero type.
STATIC BOOLEAN
InUse (IN CONST UINT8 *Entry)
{
  for (INT32 Index = 0; Index < 16; Index++) {
    if (Entry[Index]) {
      return TRUE;
    }
  }
  return FALSE;
}

STATIC BOOLEAN
Named (
  IN CONST UINT8 *Entry,
  IN CONST CHAR8 *Name
)
{
  UINTN Len = AsciiStrLen (Name);

  for (UINTN Index = 0; Index < NAME_LEN / 2; Index++) {
    UINT16 Unit = ReadLe16 (Entry + NAME_AT + 2 * Index);
    UINT16 Want = Index < Len ? (UINT8)Name[Index] : 0;
    if (Unit != Want) {
      return FALSE;
    }
    if (Unit == 0) {
      return TRUE;
    }
  }
  // All 36 units used, with no terminator.
  return Len == NAME_LEN / 2;
}

// Writes UTF-8 Name into the zeroed name field as up to 36 UTF-16 units.
STATIC VOID
PutName (
  IN OUT UINT8   *Entry,
  IN CONST CHAR8 *Name
)
{
  UINTN NameLen = AsciiStrLen (Name);
  UINTN Units   = 0;

  for (UINTN At = 0; At < NameLen && Units < NAME_LEN / 2;) {
    UINTN  CharLen;
    UINT32 CodePoint = Utf8Decode (Name + At, NameLen - At, &CharLen);
    At += CharLen;
    if (CodePoint >= 0x10000) {
      CodePoint -= 0x10000;
      WriteLe16 (
        Entry + NAME_AT + 2 * Units++, (UINT16)(0xd800 | CodePoint >> 10)
      );
      if (Units < NAME_LEN / 2) {
        WriteLe16 (
          Entry + NAME_AT + 2 * Units++, (UINT16)(0xdc00 | (CodePoint & 0x3ff))
        );
      }
    } else {
      WriteLe16 (Entry + NAME_AT + 2 * Units++, (UINT16)CodePoint);
    }
  }
}

// Finds the one in-use userdata entry. FALSE if there is none, or more than
// one, as then which to grow is a guess.
STATIC BOOLEAN
UserdataIndex (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  OUT UINTN           *Found
)
{
  UINTN   Count = EntryCount (Header, ArrayLen);
  BOOLEAN Seen  = FALSE;

  for (UINTN Index = 0; Index < Count; Index++) {
    CONST UINT8 *Entry = Array + Index * Header->EntrySize;
    if (InUse (Entry) && Named (Entry, mUserdata)) {
      if (Seen) {
        return FALSE;
      }
      Seen   = TRUE;
      *Found = Index;
    }
  }
  return Seen;
}

// Whether an entry is Linux's: in use and on disk after userdata, which
// stock Android leaves last. The disk order counts, not the entry order.
STATIC BOOLEAN
AfterUserdata (
  IN CONST UINT8 *Entry,
  IN UINT64       UserdataEnd
)
{
  return InUse (Entry) && ReadLe64 (Entry + 32) > UserdataEnd;
}

BOOLEAN
GptCfwAfterUserdata (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen
)
{
  UINTN  Count = EntryCount (Header, ArrayLen);
  UINTN  Userdata;
  UINT64 End;

  if (!UserdataIndex (Header, Array, ArrayLen, &Userdata)) {
    return FALSE;
  }
  End = ReadLe64 (Array + Userdata * Header->EntrySize + 40);
  for (UINTN Index = 0; Index < Count; Index++) {
    if (AfterUserdata (Array + Index * Header->EntrySize, End)) {
      return TRUE;
    }
  }
  return FALSE;
}

BOOLEAN
GptEntryNamed (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN UINTN             Index,
  IN CONST CHAR8      *Name
)
{
  CONST UINT8 *Entry;

  if (Index >= EntryCount (Header, ArrayLen)) {
    return FALSE;
  }
  Entry = Array + Index * Header->EntrySize;
  return InUse (Entry) && Named (Entry, Name);
}

VOID
GptWritesFree (IN OUT GPT_WRITES *Writes)
{
  for (UINTN Index = 0; Index < Writes->Count; Index++) {
    BufferFree (&Writes->Items[Index].Data);
  }
  VecFree (Writes);
}

// Sets the header CRC over its first Size bytes, with the CRC field zeroed.
STATIC VOID
SetCrc (
  IN OUT UINT8 *Header,
  IN UINTN      Size
)
{
  WriteLe32 (Header + 16, 0);
  WriteLe32 (Header + 16, CalculateCrc32 (Header, Size));
}

STATIC GPT_WRITE
WriteOf (
  IN UINT64 Lba,
  IN UINT8 *Data,
  IN UINTN  Size
)
{
  return (GPT_WRITE){Lba, {Data, Size, Size}};
}

// Whether Blocks blocks from Lba fit in [Low, High).
STATIC BOOLEAN
Within (
  IN UINT64 Lba,
  IN UINT64 Blocks,
  IN UINT64 Low,
  IN UINT64 High
)
{
  return Lba >= Low && Lba < High && Blocks <= High - Lba;
}

// Builds the writes for both tables, taking ownership of Array. The backup
// goes first, entries before header, so losing power part way leaves one
// whole table: the new backup or the old primary. BackupEntries is where
// the backup's array is; both arrays must lie outside the usable blocks.
STATIC BOOLEAN
Tables (
  IN CONST UINT8      *Header,
  IN UINTN             HeaderLen,
  IN CONST GPT_HEADER *Parsed,
  IN UINT8            *Array,
  IN UINTN             ArrayLen,
  IN UINTN             BlockSize,
  IN UINT64            BackupEntries,
  OUT GPT_WRITES      *Out
)
{
  UINTN      Size   = GptArraySize (Parsed);
  UINT64     Blocks = (Size + BlockSize - 1) / BlockSize;
  UINT8     *Primary = NULL;
  UINT8     *Backup  = NULL;
  UINT8     *Array2  = NULL;
  GPT_WRITES Writes  = {0};

  if (ArrayLen != Blocks * BlockSize ||
      !Within (Parsed->EntryLba, Blocks, 2, Parsed->FirstUsable) ||
      Parsed->LastUsable == MAX_UINT64 ||
      !Within (
        BackupEntries, Blocks, Parsed->LastUsable + 1, Parsed->AlternateLba
      )) {
    goto Fail;
  }
  Primary = AllocateCopyPool (HeaderLen, Header);
  if (!Primary) {
    goto Fail;
  }
  WriteLe32 (Primary + 88, CalculateCrc32 (Array, Size));
  SetCrc (Primary, Parsed->HeaderSize);
  Backup = AllocateCopyPool (HeaderLen, Primary);
  Array2 = AllocateCopyPool (ArrayLen, Array);
  if (!Backup || !Array2 || !VecReserve (&Writes, 4)) {
    goto Fail;
  }
  WriteLe64 (Backup + 24, Parsed->AlternateLba);
  WriteLe64 (Backup + 32, 1);
  WriteLe64 (Backup + 72, BackupEntries);
  SetCrc (Backup, Parsed->HeaderSize);
  // These can't fail; four slots are reserved.
  (VOID)VecPush (&Writes, WriteOf (BackupEntries, Array, ArrayLen));
  (VOID)VecPush (&Writes, WriteOf (Parsed->AlternateLba, Backup, HeaderLen));
  (VOID)VecPush (&Writes, WriteOf (Parsed->EntryLba, Array2, ArrayLen));
  (VOID)VecPush (&Writes, WriteOf (1, Primary, HeaderLen));
  *Out = Writes;
  return TRUE;
Fail:
  FreePool (Primary);
  FreePool (Backup);
  FreePool (Array2);
  FreePool (Array);
  VecFree (&Writes);
  return FALSE;
}

// Finds the one in-use entry called Name. FALSE if there is none, or more
// than one.
STATIC BOOLEAN
OnlyNamed (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN CONST CHAR8      *Name,
  OUT UINTN           *Found
)
{
  UINTN   Count = EntryCount (Header, ArrayLen);
  BOOLEAN Seen  = FALSE;

  for (UINTN Index = 0; Index < Count; Index++) {
    CONST UINT8 *Entry = Array + Index * Header->EntrySize;
    if (InUse (Entry) && Named (Entry, Name)) {
      if (Seen) {
        return FALSE;
      }
      Seen   = TRUE;
      *Found = Index;
    }
  }
  return Seen;
}

// What Replace Android removes.
STATIC CONST CHAR8 *CONST mReplaced[] = {
  "super", "vbmeta_system_a", "vbmeta_system_b", "userdata", GPT_BOOT_NAME,
  GPT_STORAGE_NAME};

STATIC BOOLEAN
Replaced (IN CONST UINT8 *Entry)
{
  for (UINTN Index = 0; Index < sizeof (mReplaced) / sizeof (*mReplaced);
       Index++) {
    if (Named (Entry, mReplaced[Index])) {
      return TRUE;
    }
  }
  return FALSE;
}

VOID
GptSurvey (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  OUT GPT_SURVEY      *Out
)
{
  UINTN   Count = EntryCount (Header, ArrayLen);
  UINTN   Super;
  UINTN   Userdata;
  UINTN   Boot;
  UINTN   Storage;
  BOOLEAN HasUserdata;
  BOOLEAN Any = FALSE;

  ZeroMem (Out, sizeof (*Out));
  Out->LastUsable = Header->LastUsable;
  HasUserdata     = UserdataIndex (Header, Array, ArrayLen, &Userdata);
  if (HasUserdata) {
    CONST UINT8 *Entry = Array + Userdata * Header->EntrySize;
    Out->UserdataFirst = ReadLe64 (Entry + 32);
    Out->UserdataLast  = ReadLe64 (Entry + 40);
  }
  Out->Android = HasUserdata && OnlyNamed (Header, Array, ArrayLen, "super",
                                           &Super);
  if (OnlyNamed (Header, Array, ArrayLen, GPT_BOOT_NAME, &Boot) &&
      OnlyNamed (Header, Array, ArrayLen, GPT_STORAGE_NAME, &Storage)) {
    CONST UINT8 *BootEntry    = Array + Boot * Header->EntrySize;
    CONST UINT8 *StorageEntry = Array + Storage * Header->EntrySize;
    Out->Found.BootFirst      = ReadLe64 (BootEntry + 32);
    Out->Found.BootLast       = ReadLe64 (BootEntry + 40);
    Out->Found.StorageFirst   = ReadLe64 (StorageEntry + 32);
    Out->Found.StorageLast    = ReadLe64 (StorageEntry + 40);
    CopyMem (Out->Found.BootGuid, BootEntry + 16, 16);
    Out->PortareOs = !Out->Android ||
                     (Out->Found.BootFirst > Out->UserdataLast &&
                      Out->Found.StorageFirst > Out->UserdataLast);
  }
  Out->CanShrink = Out->Android && Out->UserdataFirst <= Out->UserdataLast &&
                   Out->UserdataLast <= Header->LastUsable &&
                   !GptCfwAfterUserdata (Header, Array, ArrayLen);
  // Replace Android's space starts with the first of what it removes, and
  // nothing else may lie in it.
  Out->ReplaceFirst = MAX_UINT64;
  for (UINTN Index = 0; Index < Count; Index++) {
    CONST UINT8 *Entry = Array + Index * Header->EntrySize;
    if (InUse (Entry) && Replaced (Entry)) {
      UINT64 First = ReadLe64 (Entry + 32);
      if (First < Header->FirstUsable || ReadLe64 (Entry + 40) < First ||
          ReadLe64 (Entry + 40) > Header->LastUsable) {
        return;
      }
      Out->ReplaceFirst = First < Out->ReplaceFirst ? First : Out->ReplaceFirst;
      Any               = TRUE;
    }
  }
  if (!Any || (!Out->Android && !Out->PortareOs)) {
    return;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    CONST UINT8 *Entry = Array + Index * Header->EntrySize;
    if (InUse (Entry) && !Replaced (Entry) &&
        ReadLe64 (Entry + 40) >= Out->ReplaceFirst) {
      return;
    }
  }
  Out->CanReplace = TRUE;
}

STATIC UINT64
AlignUp64 (
  IN UINT64 Value,
  IN UINT64 Align
)
{
  return (Value + Align - 1) / Align * Align;
}

// Fills the next free entry from *Slot with a partition.
STATIC BOOLEAN
AddEntry (
  IN CONST GPT_HEADER *Header,
  IN OUT UINT8        *Array,
  IN UINTN             ArrayLen,
  IN OUT UINTN        *Slot,
  IN CONST UINT8       Type[16],
  IN CONST UINT8       Guid[16],
  IN UINT64            First,
  IN UINT64            Last,
  IN CONST CHAR8      *Name
)
{
  UINTN  Count = EntryCount (Header, ArrayLen);
  UINT8 *Entry;

  while (*Slot < Count && InUse (Array + *Slot * Header->EntrySize)) {
    (*Slot)++;
  }
  if (*Slot >= Count) {
    return FALSE;
  }
  Entry = Array + (*Slot)++ * Header->EntrySize;
  ZeroMem (Entry, Header->EntrySize);
  CopyMem (Entry, Type, 16);
  CopyMem (Entry + 16, Guid, 16);
  WriteLe64 (Entry + 32, First);
  WriteLe64 (Entry + 40, Last);
  PutName (Entry, Name);
  return TRUE;
}

// Puts PortareOS's partitions from the Align boundary at or after First to
// the last usable block, into free entries from Slot, then builds the
// writes. Takes ownership of Copy.
STATIC BOOLEAN
PlaceAndWrite (
  IN CONST UINT8       *Header,
  IN UINTN              HeaderLen,
  IN CONST GPT_HEADER  *Parsed,
  IN UINT8             *Copy,
  IN UINTN              ArrayLen,
  IN UINTN              BlockSize,
  IN UINT64             BackupEntryLba,
  IN CONST GPT_INSTALL *Install,
  IN UINT64             First,
  IN UINTN              Slot,
  OUT GPT_WRITES       *Out,
  OUT GPT_PORTAREOS    *Where
)
{
  GPT_PORTAREOS At;

  At.BootFirst    = AlignUp64 (First, Install->Align);
  At.BootLast     = At.BootFirst + Install->BootBlocks - 1;
  At.StorageFirst = At.BootLast + 1;
  At.StorageLast  = Parsed->LastUsable;
  CopyMem (At.BootGuid, Install->BootGuid, 16);
  if (At.BootFirst < First || At.StorageFirst > Parsed->LastUsable ||
      At.StorageLast - At.StorageFirst + 1 < Install->MinStorage ||
      !AddEntry (
        Parsed, Copy, ArrayLen, &Slot, gGptEfiSystem, Install->BootGuid,
        At.BootFirst, At.BootLast, GPT_BOOT_NAME
      ) ||
      !AddEntry (
        Parsed, Copy, ArrayLen, &Slot, gGptLinuxFs, Install->StorageGuid,
        At.StorageFirst, At.StorageLast, GPT_STORAGE_NAME
      )) {
    FreePool (Copy);
    return FALSE;
  }
  if (!Tables (
        Header, HeaderLen, Parsed, Copy, ArrayLen, BlockSize, BackupEntryLba,
        Out
      )) {
    return FALSE;
  }
  *Where = At;
  return TRUE;
}

// Checks what both installs need and copies the array.
STATIC UINT8 *
InstallCopy (
  IN CONST UINT8       *Header,
  IN UINTN              HeaderLen,
  IN CONST UINT8       *Array,
  IN UINTN              ArrayLen,
  IN UINTN              BlockSize,
  IN CONST GPT_INSTALL *Install,
  OUT GPT_HEADER       *Parsed,
  OUT GPT_SURVEY       *Survey
)
{
  if (!GptParse (Header, HeaderLen, Parsed) || BlockSize == 0 ||
      ArrayLen < GptArraySize (Parsed) || Install->BootBlocks == 0 ||
      Install->Align == 0 || Install->MinStorage == 0) {
    return NULL;
  }
  GptSurvey (Parsed, Array, ArrayLen, Survey);
  return AllocateCopyPool (ArrayLen, Array);
}

BOOLEAN
GptInstallDualboot (
  IN CONST UINT8       *Header,
  IN UINTN              HeaderLen,
  IN CONST UINT8       *Array,
  IN UINTN              ArrayLen,
  IN UINTN              BlockSize,
  IN UINT64             BackupEntryLba,
  IN CONST GPT_INSTALL *Install,
  OUT GPT_WRITES       *Out,
  OUT GPT_PORTAREOS    *Where
)
{
  GPT_HEADER Parsed;
  GPT_SURVEY Survey;
  UINT8     *Copy;
  UINTN      Userdata;
  UINT64     Boot;

  Copy = InstallCopy (
    Header, HeaderLen, Array, ArrayLen, BlockSize, Install, &Parsed, &Survey
  );
  if (!Copy) {
    return FALSE;
  }
  Boot = AlignUp64 (Survey.UserdataFirst + Install->AndroidBlocks,
                    Install->Align);
  if (!Survey.CanShrink || Install->AndroidBlocks == 0 ||
      Install->AndroidBlocks > Survey.UserdataLast - Survey.UserdataFirst ||
      Boot > Survey.UserdataLast ||
      !UserdataIndex (&Parsed, Copy, ArrayLen, &Userdata)) {
    FreePool (Copy);
    return FALSE;
  }
  // Userdata keeps its type; only its end moves.
  WriteLe64 (Copy + Userdata * Parsed.EntrySize + 40, Boot - 1);
  return PlaceAndWrite (
    Header, HeaderLen, &Parsed, Copy, ArrayLen, BlockSize, BackupEntryLba,
    Install, Boot, Userdata + 1, Out, Where
  );
}

BOOLEAN
GptInstallReplace (
  IN CONST UINT8       *Header,
  IN UINTN              HeaderLen,
  IN CONST UINT8       *Array,
  IN UINTN              ArrayLen,
  IN UINTN              BlockSize,
  IN UINT64             BackupEntryLba,
  IN CONST GPT_INSTALL *Install,
  OUT GPT_WRITES       *Out,
  OUT GPT_PORTAREOS    *Where
)
{
  GPT_HEADER Parsed;
  GPT_SURVEY Survey;
  UINT8     *Copy;
  UINTN      Count;

  Copy = InstallCopy (
    Header, HeaderLen, Array, ArrayLen, BlockSize, Install, &Parsed, &Survey
  );
  if (!Copy) {
    return FALSE;
  }
  if (!Survey.CanReplace) {
    FreePool (Copy);
    return FALSE;
  }
  Count = EntryCount (&Parsed, ArrayLen);
  for (UINTN Index = 0; Index < Count; Index++) {
    UINT8 *Entry = Copy + Index * Parsed.EntrySize;
    if (InUse (Entry) && Replaced (Entry)) {
      ZeroMem (Entry, Parsed.EntrySize);
    }
  }
  return PlaceAndWrite (
    Header, HeaderLen, &Parsed, Copy, ArrayLen, BlockSize, BackupEntryLba,
    Install, Survey.ReplaceFirst, 0, Out, Where
  );
}

BOOLEAN
GptVerify (
  IN CONST UINT8 *Block,
  IN UINTN        Len,
  IN CONST UINT8 *Array,
  IN UINTN        ArrayLen,
  OUT GPT_HEADER *Out
)
{
  GPT_HEADER Header;
  UINT8     *Check;
  UINT32     Crc;
  UINTN      Size;

  if (!GptParse (Block, Len, &Header)) {
    return FALSE;
  }
  // The header CRC is computed with its own field zeroed.
  Check = AllocateCopyPool (Header.HeaderSize, Block);
  if (!Check) {
    return FALSE;
  }
  WriteLe32 (Check + 16, 0);
  Crc = CalculateCrc32 (Check, Header.HeaderSize);
  FreePool (Check);
  Size = GptArraySize (&Header);
  if (Crc != ReadLe32 (Block + 16) || ArrayLen < Size ||
      CalculateCrc32 (Array, Size) != ReadLe32 (Block + 88)) {
    return FALSE;
  }
  *Out = Header;
  return TRUE;
}

BOOLEAN
GptCopiesAgree (
  IN CONST GPT_HEADER *Primary,
  IN CONST GPT_HEADER *Backup,
  IN UINT64            LastBlock
)
{
  return Primary->MyLba == 1 && Backup->MyLba == Primary->AlternateLba &&
         Backup->AlternateLba == Primary->MyLba &&
         Primary->AlternateLba > Primary->MyLba &&
         Primary->AlternateLba <= LastBlock &&
         Backup->FirstUsable == Primary->FirstUsable &&
         Backup->LastUsable == Primary->LastUsable &&
         Backup->Entries == Primary->Entries &&
         Backup->EntrySize == Primary->EntrySize &&
         Primary->FirstUsable <= Primary->LastUsable &&
         Primary->LastUsable < Primary->AlternateLba;
}

BOOLEAN
GptBuild (
  IN UINT64          DiskBlocks,
  IN UINTN           BlockSize,
  IN CONST GPT_PART *Parts,
  IN UINTN           PartCount,
  OUT OABL_BUFFER   *Out
)
{
  CONST UINTN Entries   = 128;
  CONST UINTN EntrySize = 128;
  UINTN       ArrayLen  = Entries * EntrySize;
  UINT64      ArrayBlocks;
  UINT64      Last;
  UINT64      FirstUsable;
  UINT64      LastUsable;
  UINT64      At;
  UINT8      *Disk   = NULL;
  UINT8      *Array  = NULL;
  UINT8      *Header = NULL;
  UINT8      *Backup = NULL;
  BOOLEAN     Ok     = FALSE;

  if (BlockSize < 92 || PartCount > Entries) {
    return FALSE;
  }
  ArrayBlocks = (ArrayLen + BlockSize - 1) / BlockSize;
  if (DiskBlocks < ArrayBlocks + 2 || DiskBlocks > MAX_UINTN / BlockSize) {
    return FALSE;
  }
  Last        = DiskBlocks - 1;
  FirstUsable = 2 + ArrayBlocks;
  LastUsable  = Last - ArrayBlocks - 1;
  Array       = AllocateZeroPool (ArrayLen);
  Header      = AllocateZeroPool (BlockSize);
  Backup      = AllocatePool (BlockSize);
  Disk        = AllocateZeroArray ((UINTN)DiskBlocks, BlockSize);
  if (!Array || !Header || !Backup || !Disk) {
    goto Done;
  }
  At = FirstUsable;
  for (UINTN Index = 0; Index < PartCount; Index++) {
    UINT8 *Entry = Array + Index * EntrySize;
    UINT64 End =
      Parts[Index].Blocks == 0 ? LastUsable : At + Parts[Index].Blocks - 1;
    CopyMem (Entry, Parts[Index].Type, 16);
    Entry[16] = (UINT8)(Index + 1);
    Entry[31] = 0x5a;
    WriteLe64 (Entry + 32, At);
    WriteLe64 (Entry + 40, End);
    PutName (Entry, Parts[Index].Name);
    At = End + 1;
  }
  CopyMem (Header, GPT_SIGNATURE, 8);
  WriteLe32 (Header + 8, 0x00010000);
  WriteLe32 (Header + 12, 92);
  WriteLe64 (Header + 24, 1);
  WriteLe64 (Header + 32, Last);
  WriteLe64 (Header + 40, FirstUsable);
  WriteLe64 (Header + 48, LastUsable);
  SetMem (Header + 56, 16, 0x11);
  WriteLe64 (Header + 72, 2);
  WriteLe32 (Header + 80, (UINT32)Entries);
  WriteLe32 (Header + 84, (UINT32)EntrySize);
  WriteLe32 (Header + 88, CalculateCrc32 (Array, ArrayLen));
  SetCrc (Header, 92);
  CopyMem (Backup, Header, BlockSize);
  WriteLe64 (Backup + 24, Last);
  WriteLe64 (Backup + 32, 1);
  WriteLe64 (Backup + 72, Last - ArrayBlocks);
  SetCrc (Backup, 92);
  // Protective MBR.
  Disk[446 + 4] = 0xee;
  Disk[510]     = 0x55;
  Disk[511]     = 0xaa;
  CopyMem (Disk + BlockSize, Header, BlockSize);
  CopyMem (Disk + 2 * BlockSize, Array, ArrayLen);
  CopyMem (Disk + (UINTN)(Last - ArrayBlocks) * BlockSize, Array, ArrayLen);
  CopyMem (Disk + (UINTN)Last * BlockSize, Backup, BlockSize);
  *Out = (OABL_BUFFER){Disk, (UINTN)DiskBlocks * BlockSize,
                       (UINTN)DiskBlocks * BlockSize};
  Disk = NULL;
  Ok   = TRUE;
Done:
  FreePool (Array);
  FreePool (Header);
  FreePool (Backup);
  FreePool (Disk);
  return Ok;
}

BOOLEAN
GptReadBack (
  IN CONST UINT8 *Disk,
  IN UINTN        DiskLen,
  IN UINTN        BlockSize,
  IN UINT64       HeaderLba,
  OUT GPT_HEADER *Header,
  OUT OABL_BYTES *Array
)
{
  UINTN        At;
  GPT_HEADER   Parsed;
  CONST UINT8 *Block;

  if (BlockSize && HeaderLba >= MAX_UINTN / BlockSize) {
    return FALSE;
  }
  At = (UINTN)HeaderLba * BlockSize;
  if (!InBounds (DiskLen, At, BlockSize)) {
    return FALSE;
  }
  Block = Disk + At;
  if (!GptParse (Block, BlockSize, &Parsed)) {
    return FALSE;
  }
  if (Parsed.EntryLba > MAX_UINTN / BlockSize) {
    return FALSE;
  }
  At = (UINTN)Parsed.EntryLba * BlockSize;
  if (!InBounds (DiskLen, At, GptArraySize (&Parsed))) {
    return FALSE;
  }
  if (!GptVerify (
        Block, BlockSize, Disk + At, GptArraySize (&Parsed), Header
      )) {
    return FALSE;
  }
  *Array = BytesOf (Disk + At, GptArraySize (&Parsed));
  return TRUE;
}

BOOLEAN
GptEntryType (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN UINTN             Index,
  OUT UINT8            Type[16]
)
{
  CONST UINT8 *Entry;

  if (Index >= EntryCount (Header, ArrayLen)) {
    return FALSE;
  }
  Entry = Array + Index * Header->EntrySize;
  if (!InUse (Entry)) {
    return FALSE;
  }
  CopyMem (Type, Entry, 16);
  return TRUE;
}

BOOLEAN
GptEntryGuid (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN UINTN             Index,
  OUT UINT8            Guid[16]
)
{
  CONST UINT8 *Entry;

  if (Index >= EntryCount (Header, ArrayLen)) {
    return FALSE;
  }
  Entry = Array + Index * Header->EntrySize;
  if (!InUse (Entry)) {
    return FALSE;
  }
  CopyMem (Guid, Entry + 16, 16);
  return TRUE;
}

BOOLEAN
GptExtent (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN UINTN             Index,
  OUT UINT64          *Start,
  OUT UINT64          *End
)
{
  CONST UINT8 *Entry;

  if (Index >= EntryCount (Header, ArrayLen)) {
    return FALSE;
  }
  Entry = Array + Index * Header->EntrySize;
  if (!InUse (Entry)) {
    return FALSE;
  }
  *Start = ReadLe64 (Entry + 32);
  *End   = ReadLe64 (Entry + 40);
  return TRUE;
}
