/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  NestedDisk - a disk image written to userdata, as a disk of its own.
*/

#include "Efi/NestedDisk.h"
#include "Base/Memory.h"

#include "Core/EfiData.h"
#include "Efi/Files.h"

#define BOUNCE_SIZE  ((UINTN)64 << 10)
#define BOUNCE_PAGES (BOUNCE_SIZE / 4096)

typedef struct {
  EFI_BLOCK_IO_PROTOCOL  BlockIo;
  EFI_BLOCK_IO_MEDIA     Media;
  EFI_BLOCK_IO_PROTOCOL *Parent;
  UINT8                 *Bounce;
} NESTED_DISK;

STATIC UINT8 *
BounceNew (VOID)
{
  EFI_PHYSICAL_ADDRESS Addr;

  if (gBS->AllocatePages (
        AllocateAnyPages, EfiBootServicesData, BOUNCE_PAGES, &Addr
      ) != EFI_SUCCESS) {
    return NULL;
  }
  return (UINT8 *)(UINTN)Addr;
}

STATIC VOID
BounceFree (IN UINT8 *Bounce)
{
  gBS->FreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)Bounce, BOUNCE_PAGES);
}

// Reads Size bytes at Offset, whatever the parent's block size.
STATIC EFI_STATUS
ParentRead (
  IN EFI_BLOCK_IO_PROTOCOL *Parent,
  IN UINT8                 *Bounce,
  IN UINT64                 Offset,
  OUT UINT8                *Out,
  IN UINTN                  Size
)
{
  UINTN Bs = Parent->Media->BlockSize;

  while (Size > 0) {
    UINT64     Lba   = Offset / Bs;
    UINTN      Skip  = (UINTN)(Offset % Bs);
    UINTN      Bytes = (Skip + Size + Bs - 1) / Bs * Bs;
    UINTN      Take;
    EFI_STATUS Status;

    if (Bytes > BOUNCE_SIZE) {
      Bytes = BOUNCE_SIZE;
    }
    Take   = Bytes - Skip < Size ? Bytes - Skip : Size;
    Status = Parent->ReadBlocks (
      Parent, Parent->Media->MediaId, Lba, Bytes, Bounce
    );
    if (Status != EFI_SUCCESS) {
      return Status;
    }
    CopyMem (Out, Bounce + Skip, Take);
    Offset += Take;
    Out += Take;
    Size -= Take;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
DiskReset (IN EFI_BLOCK_IO_PROTOCOL *This, IN BOOLEAN Ext)
{
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
DiskRead (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  Size,
  OUT VOID                 *Buffer
)
{
  NESTED_DISK *D = (NESTED_DISK *)This;

  if (Size % D->Media.BlockSize || Lba > D->Media.LastBlock ||
      Size / D->Media.BlockSize > D->Media.LastBlock - Lba + 1) {
    return EFI_INVALID_PARAMETER;
  }
  return ParentRead (
    D->Parent, D->Bounce, Lba * D->Media.BlockSize, Buffer, Size
  );
}

STATIC EFI_STATUS EFIAPI
DiskWrite (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  Size,
  IN VOID                  *Buffer
)
{
  return EFI_WRITE_PROTECTED;
}

STATIC EFI_STATUS EFIAPI
DiskFlush (IN EFI_BLOCK_IO_PROTOCOL *This)
{
  return EFI_SUCCESS;
}

// Qualcomm's PartitionDxe puts the GPT entry on each partition, SDM845's
// has no EFI_PARTITION_INFO_PROTOCOL.
STATIC EFI_GUID mPartitionRecordGuid = {
  0xfe2555be, 0xd716, 0x4686, {0xb9, 0xd0, 0x79, 0xdb, 0x59, 0x21, 0xb7, 0x0d}
};

STATIC BOOLEAN
IsUserdata (IN EFI_HANDLE Handle)
{
  EFI_PARTITION_INFO_PROTOCOL *Info =
    GetProtocol (Handle, &gEfiPartitionInfoProtocolGuid);
  CONST UINT8                 *Record = GetProtocol (Handle, &mPartitionRecordGuid);
  CONST CHAR16                *Have;
  STATIC CONST CHAR16          Name[] = L"userdata";

  if (Info && Info->Type == PARTITION_TYPE_GPT) {
    Have = Info->Info.Gpt.PartitionName;
  } else if (Record) {
    Have = (CONST CHAR16 *)(Record + 56);
  } else {
    return FALSE;
  }
  for (UINTN I = 0; I < sizeof (Name) / sizeof (*Name); I++) {
    if (Have[I] != Name[I]) {
      return FALSE;
    }
  }
  return TRUE;
}

// Reads and verifies the GPT at sector 1 of userdata, Ss bytes a sector.
STATIC BOOLEAN
Probe (
  IN EFI_BLOCK_IO_PROTOCOL *P,
  IN UINT8                 *Bounce,
  IN UINT32                 Ss,
  OUT NESTED_GPT           *Out
)
{
  UINT64      Sectors = (P->Media->LastBlock + 1) * P->Media->BlockSize / Ss;
  UINT8       Block[4096];
  OABL_BUFFER Array = {0};
  GPT_HEADER  Header;
  UINTN       Size;

  if (ParentRead (P, Bounce, Ss, Block, Ss) != EFI_SUCCESS ||
      !GptParse (Block, Ss, &Header)) {
    return FALSE;
  }
  Size = GptArraySize (&Header);
  if (Header.MyLba != 1 || Header.AlternateLba < 2 ||
      Header.AlternateLba >= Sectors || Header.EntryLba >= Sectors ||
      Header.EntryLba * Ss + Size > Sectors * Ss ||
      !BufferReserve (&Array, Size)) {
    return FALSE;
  }
  Array.Len = Size;
  if (ParentRead (P, Bounce, Header.EntryLba * Ss, Array.Data, Size) !=
        EFI_SUCCESS ||
      !GptVerify (Block, Ss, Array.Data, Size, &Header)) {
    BufferFree (&Array);
    return FALSE;
  }
  Out->BlockIo    = P;
  Out->SectorSize = Ss;
  Out->Header     = Header;
  Out->Array      = Array;
  return TRUE;
}

BOOLEAN
NestedDiskFind (OUT NESTED_GPT *Out)
{
  EFI_HANDLE *Handles;
  UINT8      *Bounce = BounceNew ();
  UINTN       Count  = LocateHandles (&gEfiBlockIoProtocolGuid, &Handles);
  BOOLEAN     Found  = FALSE;

  for (UINTN Index = 0; Bounce && Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL *P = GetProtocol (Handles[Index], &gEfiBlockIoProtocolGuid);

    if (!P || !P->Media->LogicalPartition || !IsUserdata (Handles[Index])) {
      continue;
    }
    Found = Probe (P, Bounce, 512, Out) || Probe (P, Bounce, 4096, Out);
    Out->Handle = Handles[Index];
    break;
  }
  if (Bounce) {
    BounceFree (Bounce);
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  return Found;
}

// Whether PartitionDxe already found a GPT in Parent (In the case of SDM845).
STATIC BOOLEAN
HasGptChildren (IN CONST OABL_BUFFER *Parent)
{
  EFI_HANDLE *Handles;
  UINTN       Count  = LocateHandles (&gEfiBlockIoProtocolGuid, &Handles);
  UINTN       Prefix = Parent->Len - 4;
  BOOLEAN     Child  = FALSE;

  for (UINTN Index = 0; Index < Count && !Child; Index++) {
    OABL_BUFFER Path;

    if (!PathsHandlePath (Handles[Index], &Path)) {
      continue;
    }
    Child = Path.Len >= Prefix + 42 && CompareMem (Path.Data, Parent->Data, Prefix) == 0 &&
            Path.Data[Prefix] == 4 && Path.Data[Prefix + 1] == 1 && Path.Data[Prefix + 41] == 2;
    BufferFree (&Path);
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  return Child;
}

VOID
NestedDiskPublish (IN LOG_SINK Log)
{
  STATIC CONST UINT8 VenNode[20] = {
    0x04, 0x03, 20, 0,
    0x4e, 0x65, 0x73, 0x74, 0x65, 0x64, 0x44, 0x69,
    0x73, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
  };
  STATIC BOOLEAN Done;
  NESTED_GPT     Nested;
  NESTED_DISK   *D;
  OABL_BUFFER    Parent = {0};
  OABL_BUFFER    Path   = {0};
  EFI_HANDLE     Handle = NULL;
  EFI_STATUS     Status;

  if (Done) {
    return;
  }
  Done = TRUE;
  if (!NestedDiskFind (&Nested)) {
    return;
  }
  BufferFree (&Nested.Array);
  if (!PathsHandlePath (Nested.Handle, &Parent)) {
    return;
  }
  if (HasGptChildren (&Parent)) {
    LogLine (Log, "nested: XBL reads the GPT in userdata itself");
    BufferFree (&Parent);
    return;
  }
  D = AllocateZeroPool (sizeof (*D));
  if (!D || !(D->Bounce = BounceNew ()) ||
      !UefiJoin (
        (OABL_BYTES[]){BufferBytes (&Parent), {VenNode, sizeof (VenNode)}},
        2, &Path
      )) {
    BufferFree (&Parent);
    return;
  }
  BufferFree (&Parent);
  D->Parent = Nested.BlockIo;
  D->Media  = (EFI_BLOCK_IO_MEDIA){
    .MediaId          = Nested.BlockIo->Media->MediaId,
    .MediaPresent     = TRUE,
    .LogicalPartition = FALSE,
    .ReadOnly         = TRUE,
    .BlockSize        = Nested.SectorSize,
    .LastBlock        = Nested.Header.AlternateLba,
  };
  D->BlockIo = (EFI_BLOCK_IO_PROTOCOL){
    .Revision    = EFI_BLOCK_IO_PROTOCOL_REVISION,
    .Media       = &D->Media,
    .Reset       = DiskReset,
    .ReadBlocks  = DiskRead,
    .WriteBlocks = DiskWrite,
    .FlushBlocks = DiskFlush,
  };
  Status = gBS->InstallMultipleProtocolInterfaces (
    &Handle, &gEfiDevicePathProtocolGuid, Path.Data,
    &gEfiBlockIoProtocolGuid, &D->BlockIo, NULL
  );
  if (Status == EFI_SUCCESS) {
    Status = gBS->ConnectController (Handle, NULL, NULL, TRUE);
  }
  LogPrint (
    Log, "nested: userdata as a disk of %llu %u-byte sectors: 0x%llx",
    (UINT64)(Nested.Header.AlternateLba + 1), Nested.SectorSize, (UINT64)Status
  );
}
