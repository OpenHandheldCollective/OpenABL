/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Uninstall - finds Linux on internal storage and removes it.
*/

#include "Efi/Uninstall.h"
#include "Base/Memory.h"

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

// Returns the disk Linux is installed on, or NULL.
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

    if (!BlockIo || !UninstallTable (BlockIo, &Block, &Header, &Array)) {
      continue;
    }
    if (GptCfwAfterUserdata (&Header, Array.Data, Array.Len)) {
      Found = Handles[Index];
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
  BOOLEAN Installed;

  if (mFoundCfw) {
    return mFoundCfw == 2;
  }
  QuirksConnectAll (Chip, Log);
  Installed = Find () != NULL;
  mFoundCfw = Installed ? 2 : 1;
  LogLine (
    Log, Installed ? "cfw: Linux is installed on internal storage"
                   : "cfw: not on internal storage"
  );
  return Installed;
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
  EFI_STATUS             Status = EFI_NOT_FOUND;

  if (!Disk) {
    LogLine (Log, "uninstall: Linux isn't on internal storage");
    return EFI_NOT_FOUND;
  }
  BlockIo = UninstallBlockIo (Disk);
  if (!BlockIo || !UninstallTable (BlockIo, &Block, &Header, &Array)) {
    return EFI_NOT_FOUND;
  }
  if (!GptUninstall (
        Block.Data, Block.Len, Array.Data, Array.Len, BlockIo->Media->BlockSize,
        &Writes
      )) {
    goto Done;
  }
  for (UINTN Index = 0; Index < Writes.Count; Index++) {
    Status = WriteBlocks (
      BlockIo, Writes.Items[Index].Lba, Writes.Items[Index].Data.Data,
      Writes.Items[Index].Data.Len
    );
    if (Status != EFI_SUCCESS) {
      LogPrint (
        Log, "uninstall: writing block %llu failed",
        (UINT64)Writes.Items[Index].Lba
      );
      goto Done;
    }
  }
  Status = BlockIo->FlushBlocks (BlockIo);
  if (Status != EFI_SUCCESS) {
    goto Done;
  }
  mFoundCfw = 1;
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
