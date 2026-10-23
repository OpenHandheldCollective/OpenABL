/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Disk - block I/O and partition tables on fixed disks.
*/

#include "Efi/Disk.h"
#include "Base/Memory.h"

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
DiskBlockIo (IN EFI_HANDLE Handle)
{
  return GetProtocol (Handle, &gEfiBlockIoProtocolGuid);
}

BOOLEAN
DiskRead (
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

EFI_STATUS
DiskWrite (
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
DiskTable (
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
  if (!DiskRead (BlockIo, 1, BlockSize, &HeaderBlock)) {
    return FALSE;
  }
  // GptParse fails on an empty block, so BlockSize isn't 0 below.
  if (!GptParse (HeaderBlock.Data, HeaderBlock.Len, &Parsed) || !BlockSize) {
    BufferFree (&HeaderBlock);
    return FALSE;
  }
  Size = GptArraySize (&Parsed);
  if (!DiskRead (
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

BOOLEAN
DiskCheckedTable (
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
      !DiskTable (BlockIo, Block, Header, Array)) {
    return FALSE;
  }
  Size = GptArraySize (Header);
  Ok   = GptVerify (Block->Data, Block->Len, Array->Data, Array->Len, Header) &&
         DiskRead (BlockIo, Header->AlternateLba, BlockSize, &BackupBlock) &&
         GptParse (BackupBlock.Data, BackupBlock.Len, Backup) &&
         GptCopiesAgree (Header, Backup, BlockIo->Media->LastBlock) &&
         DiskRead (
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

// Whether a fixed disk's checked table has Linux after userdata.
STATIC BOOLEAN
Found (VOID)
{
  EFI_HANDLE *Handles;
  BOOLEAN     Any   = FALSE;
  UINTN       Count = LocateHandles (&gEfiBlockIoProtocolGuid, &Handles);

  for (UINTN Index = 0; Index < Count && !Any; Index++) {
    EFI_BLOCK_IO_PROTOCOL *BlockIo = DiskBlockIo (Handles[Index]);
    OABL_BUFFER            Block   = {0};
    OABL_BUFFER            Array   = {0};
    GPT_HEADER             Header;
    GPT_HEADER             Backup;

    if (!BlockIo ||
        !DiskCheckedTable (BlockIo, &Block, &Header, &Array, &Backup)) {
      continue;
    }
    Any = GptCfwAfterUserdata (&Header, Array.Data, Array.Len);
    BufferFree (&Block);
    BufferFree (&Array);
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  return Any;
}

// 0: not looked yet, 1: not installed, 2: installed.
STATIC UINT8 mLinux;

BOOLEAN
DiskLinuxOnInternal (
  IN UINT32   Chip,
  IN LOG_SINK Log
)
{
  BOOLEAN    Installed;
  BOOLEAN    Image;
  NESTED_GPT Nested;

  if (mLinux) {
    return mLinux == 2;
  }
  QuirksConnectAll (Chip, Log);
  Installed = Found ();
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
  mLinux    = Installed ? 2 : 1;
  return Installed;
}
