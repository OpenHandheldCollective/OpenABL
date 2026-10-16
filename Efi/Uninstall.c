/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Uninstall - finds Linux on internal storage and removes it.
*/

#include "Efi/Uninstall.h"
#include "Base/Memory.h"

#include "Core/AndroidImage.h"
#include "Efi/NestedDisk.h"
#include "Efi/Quirks.h"

// Allocates whole pages for Length bytes (at least one), so the buffer
// suits any IoAlign.
STATIC BOOLEAN
PagesNew (
  IN UINTN                  Length,
  OUT EFI_PHYSICAL_ADDRESS *Address,
  OUT UINTN                *Pages
)
{
  *Pages = Length / 4096 + (Length % 4096 != 0);
  if (*Pages == 0) {
    *Pages = 1;
  }
  return gBS->AllocatePages (
           AllocateAnyPages, EfiLoaderData, *Pages, Address
         ) == EFI_SUCCESS;
}

EFI_BLOCK_IO_PROTOCOL *
UninstallBlockIo (IN EFI_HANDLE Handle)
{
  return GetProtocol (Handle, &gEfiBlockIoProtocolGuid);
}

BOOLEAN
UninstallRead (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
  IN UINT64                 Lba,
  IN UINTN                  Length,
  OUT OABL_BUFFER          *Out
)
{
  EFI_PHYSICAL_ADDRESS Address;
  UINTN                Pages;
  UINT8               *Buffer;
  BOOLEAN              Ok;

  if (!PagesNew (Length, &Address, &Pages)) {
    return FALSE;
  }
  Buffer = (UINT8 *)(UINTN)Address;
  Ok     = BlockIo->ReadBlocks (
             BlockIo, BlockIo->Media->MediaId, Lba, Length, Buffer
           ) == EFI_SUCCESS &&
           BufferFrom (Out, Buffer, Length);
  gBS->FreePages (Address, Pages);
  return Ok;
}

STATIC EFI_STATUS
WriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
  IN UINT64                 Lba,
  IN CONST UINT8           *Data,
  IN UINTN                  Length
)
{
  EFI_PHYSICAL_ADDRESS Address;
  UINTN                Pages;
  EFI_STATUS           Status;

  if (!PagesNew (Length, &Address, &Pages)) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem ((UINT8 *)(UINTN)Address, Data, Length);
  Status = BlockIo->WriteBlocks (
    BlockIo, BlockIo->Media->MediaId, Lba, Length, (VOID *)(UINTN)Address
  );
  gBS->FreePages (Address, Pages);
  return Status;
}

BOOLEAN
UninstallTable (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
  OUT OABL_BUFFER          *Block,
  OUT GPT_HEADER           *Header,
  OUT OABL_BUFFER          *Array
)
{
  EFI_BLOCK_IO_MEDIA *Media       = BlockIo->Media;
  OABL_BUFFER         HeaderBlock = {0};
  OABL_BUFFER         Entries     = {0};
  GPT_HEADER          Parsed;
  UINTN               BlockSize;
  UINTN               Size;

  if (!Media->MediaPresent || Media->LogicalPartition ||
      Media->RemovableMedia) {
    return FALSE;
  }
  BlockSize = Media->BlockSize;
  if (!UninstallRead (BlockIo, 1, BlockSize, &HeaderBlock)) {
    return FALSE;
  }
  // GptParse fails on an empty block, so BlockSize isn't 0 below.
  if (!GptParse (HeaderBlock.Data, HeaderBlock.Len, &Parsed) || !BlockSize) {
    BufferFree (&HeaderBlock);
    return FALSE;
  }
  Size = GptArraySize (&Parsed);
  if (!UninstallRead (
        BlockIo, Parsed.EntryLba,
        (Size / BlockSize + (Size % BlockSize != 0)) * BlockSize, &Entries
      )) {
    BufferFree (&HeaderBlock);
    return FALSE;
  }
  *Block  = HeaderBlock;
  *Header = Parsed;
  *Array  = Entries;
  return TRUE;
}

// Reads the table like UninstallTable, then also requires a writable disk,
// valid CRCs in both copies, headers that agree on the layout
// (GptCopiesAgree) and the same entries in each. Linux's partitions are
// only removed from a table that passes. Backup gets the backup's header.
STATIC BOOLEAN
CheckedTable (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
  OUT OABL_BUFFER          *Block,
  OUT GPT_HEADER           *Header,
  OUT OABL_BUFFER          *Array,
  OUT GPT_HEADER           *Backup
)
{
  UINTN       BlockSize   = BlockIo->Media->BlockSize;
  OABL_BUFFER BackupBlock = {0};
  OABL_BUFFER BackupArray = {0};
  UINTN       Size;
  BOOLEAN     Ok;

  if (BlockIo->Media->ReadOnly ||
      !UninstallTable (BlockIo, Block, Header, Array)) {
    return FALSE;
  }
  Size = GptArraySize (Header);
  Ok   = GptVerify (Block->Data, Block->Len, Array->Data, Array->Len, Header) &&
         UninstallRead (BlockIo, Header->AlternateLba, BlockSize, &BackupBlock) &&
         GptParse (BackupBlock.Data, BackupBlock.Len, Backup) &&
         GptCopiesAgree (Header, Backup, BlockIo->Media->LastBlock) &&
         UninstallRead (
           BlockIo, Backup->EntryLba,
           (Size / BlockSize + (Size % BlockSize != 0)) * BlockSize,
           &BackupArray
         ) &&
         GptVerify (
           BackupBlock.Data, BackupBlock.Len, BackupArray.Data,
           BackupArray.Len, Backup
         ) &&
         CompareMem (Array->Data, BackupArray.Data, Size) == 0;
  BufferFree (&BackupBlock);
  BufferFree (&BackupArray);
  if (!Ok) {
    BufferFree (Block);
    BufferFree (Array);
  }
  return Ok;
}

// The partitions Linux added, for the confirmation; filled with mFoundCfw.
STATIC GPT_CFW_PART mParts[UNINSTALL_MAX_PARTS];
STATIC UINTN        mPartCount;

// Returns the disk Linux is installed on, or NULL. Fills mParts.
STATIC EFI_HANDLE
Find (VOID)
{
  EFI_HANDLE *Handles;
  EFI_HANDLE  Found = NULL;
  UINTN       Count = LocateHandles (&gEfiBlockIoProtocolGuid, &Handles);

  for (UINTN Index = 0; Index < Count && !Found; Index++) {
    EFI_BLOCK_IO_PROTOCOL *BlockIo = UninstallBlockIo (Handles[Index]);
    OABL_BUFFER            Block   = {0};
    OABL_BUFFER            Array   = {0};
    GPT_HEADER             Header;
    GPT_HEADER             Backup;
    UINTN                  Parts;

    if (!BlockIo ||
        !CheckedTable (BlockIo, &Block, &Header, &Array, &Backup)) {
      continue;
    }
    Parts = GptCfwParts (
      &Header, Array.Data, Array.Len, BlockIo->Media->BlockSize, mParts,
      UNINSTALL_MAX_PARTS
    );
    if (Parts) {
      Found      = Handles[Index];
      mPartCount = Parts < UNINSTALL_MAX_PARTS ? Parts : UNINSTALL_MAX_PARTS;
    }
    BufferFree (&Block);
    BufferFree (&Array);
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  return Found;
}

// 0: not looked yet, 1: not installed, 2: installed.
STATIC UINT8 mFoundCfw;

BOOLEAN
UninstallCfwOnInternal (
  IN UINT32   Chip,
  IN LOG_SINK Log
)
{
  BOOLEAN    Installed;
  BOOLEAN    Image;
  NESTED_GPT Nested;

  if (mFoundCfw) {
    return mFoundCfw == 2;
  }
  QuirksConnectAll (Chip, Log);
  Installed = Find () != NULL;
  Image     = NestedDiskFind (&Nested);
  if (Image) {
    BufferFree (&Nested.Array);
  }
  LogLine (
    Log, Installed ? "cfw: Linux is installed on internal storage"
         : Image   ? "cfw: Linux is installed as an image in userdata"
                   : "cfw: not on internal storage"
  );
  Installed = Installed || Image;
  mFoundCfw = Installed ? 2 : 1;
  return Installed;
}

UINTN
UninstallCfwParts (OUT CONST GPT_CFW_PART **Parts)
{
  *Parts = mParts;
  return mFoundCfw == 2 ? mPartCount : 0;
}

EFI_STATUS
UninstallRun (IN LOG_SINK Log)
{
  EFI_HANDLE             Disk = Find ();
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  OABL_BUFFER            Block  = {0};
  OABL_BUFFER            Array  = {0};
  GPT_WRITES             Writes = {0};
  GPT_HEADER             Header;
  GPT_HEADER             Backup;
  EFI_STATUS             Status = EFI_NOT_FOUND;

  if (!Disk) {
    LogLine (Log, "uninstall: Linux isn't on internal storage");
    return EFI_NOT_FOUND;
  }
  BlockIo = UninstallBlockIo (Disk);
  if (!BlockIo || !CheckedTable (BlockIo, &Block, &Header, &Array, &Backup)) {
    LogLine (Log, "uninstall: the table can't be read again");
    return EFI_VOLUME_CORRUPTED;
  }
  if (!GptUninstall (
        Block.Data, Block.Len, Array.Data, Array.Len, BlockIo->Media->BlockSize,
        Backup.EntryLba, &Writes
      )) {
    LogLine (Log, "uninstall: the table's layout can't be trusted");
    Status = EFI_VOLUME_CORRUPTED;
    goto Done;
  }
  BufferFree (&Block);
  BufferFree (&Array);
  // Flushed one by one, so the order GptUninstall chose reaches the disk.
  for (UINTN Index = 0; Index < Writes.Count; Index++) {
    Status = WriteBlocks (
      BlockIo, Writes.Items[Index].Lba, Writes.Items[Index].Data.Data,
      Writes.Items[Index].Data.Len
    );
    if (Status == EFI_SUCCESS) {
      Status = BlockIo->FlushBlocks (BlockIo);
    }
    if (Status != EFI_SUCCESS) {
      LogPrint (
        Log, "uninstall: writing block %llu failed",
        (UINT64)Writes.Items[Index].Lba
      );
      goto Done;
    }
  }
  // Read both tables back, as the next boot will.
  if (!CheckedTable (BlockIo, &Block, &Header, &Array, &Backup) ||
      GptCfwAfterUserdata (&Header, Array.Data, Array.Len)) {
    LogLine (Log, "uninstall: the tables didn't read back right");
    Status = EFI_CRC_ERROR;
    goto Done;
  }
  mFoundCfw  = 1;
  mPartCount = 0;
  LogLine (
    Log,
    "uninstall: Linux removed from internal storage; userdata spans the rest"
  );
Done:
  GptWritesFree (&Writes);
  BufferFree (&Block);
  BufferFree (&Array);
  return Status;
}

// Finds misc on any disk with a primary GPT whose CRCs pass: its disk and
// first and last block. FALSE if there is none, or more than one, as then
// which one recovery reads is a guess.
STATIC BOOLEAN
FindMisc (
  OUT EFI_BLOCK_IO_PROTOCOL **Found,
  OUT UINT64                 *First,
  OUT UINT64                 *Last,
  IN LOG_SINK                 Log
)
{
  EFI_HANDLE *Handles;
  UINTN       Count = LocateHandles (&gEfiBlockIoProtocolGuid, &Handles);
  UINTN       Miscs = 0;

  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL *BlockIo = UninstallBlockIo (Handles[Index]);
    OABL_BUFFER            Block   = {0};
    OABL_BUFFER            Array   = {0};
    GPT_HEADER             Header;
    BOOLEAN                Verified;

    if (!BlockIo || BlockIo->Media->ReadOnly ||
        !UninstallTable (BlockIo, &Block, &Header, &Array)) {
      continue;
    }
    Verified =
      GptVerify (Block.Data, Block.Len, Array.Data, Array.Len, &Header);
    for (UINTN Entry = 0; Verified && Entry < Header.Entries; Entry++) {
      // Every misc is counted; the first one found is kept.
      if (GptEntryNamed (&Header, Array.Data, Array.Len, Entry, "misc") &&
          Miscs++ == 0 &&
          GptExtent (&Header, Array.Data, Array.Len, Entry, First, Last)) {
        *Found = BlockIo;
      }
    }
    BufferFree (&Block);
    BufferFree (&Array);
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  if (Miscs > 1) {
    LogPrint (Log, "uninstall: %llu misc partitions", (UINT64)Miscs);
  }
  return Miscs == 1;
}

EFI_STATUS
UninstallRequestWipe (IN LOG_SINK Log)
{
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  OABL_BUFFER            Blocks = {0};
  UINT8                  Message[ANDROID_BCB_SIZE];
  UINT64                 First;
  UINT64                 Last;
  UINTN                  BlockSize;
  UINTN                  Length;
  EFI_STATUS             Status;

  if (!FindMisc (&BlockIo, &First, &Last, Log)) {
    LogLine (Log, "uninstall: no single misc partition");
    return EFI_NOT_FOUND;
  }
  BlockSize = BlockIo->Media->BlockSize;
  Length    = (ANDROID_BCB_SIZE + BlockSize - 1) / BlockSize * BlockSize;
  if (Last < First || (Last - First + 1) * BlockSize < Length) {
    return EFI_BAD_BUFFER_SIZE;
  }
  // Keep whatever else shares misc's first blocks.
  if (!UninstallRead (BlockIo, First, Length, &Blocks)) {
    return EFI_DEVICE_ERROR;
  }
  AndroidBcbWipe (Message);
  CopyMem (Blocks.Data, Message, ANDROID_BCB_SIZE);
  Status = WriteBlocks (BlockIo, First, Blocks.Data, Length);
  if (Status == EFI_SUCCESS) {
    Status = BlockIo->FlushBlocks (BlockIo);
  }
  BufferFree (&Blocks);
  LogLine (
    Log, Status == EFI_SUCCESS ? "uninstall: asked recovery to wipe data"
                               : "uninstall: writing misc failed"
  );
  return Status;
}
