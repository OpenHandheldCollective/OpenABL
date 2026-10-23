/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Install - copies PortareOS from the SD card to internal storage.
*/

#include "Efi/Install.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Core/EfiData.h"
#include "Core/Ext4Make.h"
#include "Core/Fat32.h"
#include "Core/Fat32Make.h"
#include "Efi/Files.h"
#include "Efi/Disk.h"

#include <Protocol/EFIChargerEx.h>
#include <Protocol/EFIEraseBlock.h>
#include <Protocol/EFIRng.h>

EFI_GUID gEfiEraseBlockProtocolGuid = EFI_ERASE_BLOCK_PROTOCOL_GUID;
EFI_GUID gQcomRngProtocolGuid       = {
  0x3152bca5, 0xeade, 0x433d, {0x86, 0x2e, 0xc0, 0x1c, 0xdc, 0x29, 0x1f, 0x44}
};
EFI_GUID gEfiRNGAlgRawGuid = {
  0xe43176d7, 0xb6e8, 0x4827, {0xb7, 0x84, 0x7f, 0xfd, 0xc4, 0xb6, 0x85, 0x61}
};

#define GIB (1ull << 30)
#define MIB (1ull << 20)
#define BOOT_BYTES INSTALL_BOOT_BYTES
#define MIN_STORAGE (8 * GIB)
#define MIN_ANDROID (4 * GIB)
// Partition starts on erase-block-friendly boundaries.
#define ALIGN_BYTES (4 * MIB)
// Zeroed at the start of userdata, so Android finds it blank and formats
// it; installtointernal does the same.
#define USERDATA_ZERO (8 * MIB)
// Zeroed at the start of a new partition, over old file system signatures.
#define HEAD_ZERO (1 * MIB)
#define CHUNK     (4u * 1024 * 1024)
// Storage: one inode per 32 KiB and a 128 MiB journal (see Core/Ext4Make).
#define INODE_RATIO    32768
#define JOURNAL_BLOCKS 32768
#define MAX_FILES      4096
#define MAX_DEPTH      8
#define LABEL          "PORTAREOS"

#define SAY(...) LogPrint (Log, "install: " __VA_ARGS__)

// Folders a Mac or Windows leaves on a card, not copied.
STATIC CONST CHAR8 *CONST mSkipped[] = {
  ".Spotlight-V100", ".fseventsd", ".Trashes", "System Volume Information"};

typedef struct _CARD_PATHS OABL_VEC (CHAR8 *) CARD_PATHS;
typedef struct _CARD_NODES OABL_VEC (FAT32_NODE) CARD_NODES;

// The card's PortareOS: its file system and files, each with its path.
typedef struct {
  EFI_HANDLE Fs;
  CARD_NODES Nodes;
  CARD_PATHS Paths;
  UINT64     Bytes;
  UINT16     Date;
  UINT16     Time;
} CARD;

// The disk installed onto.
typedef struct {
  EFI_HANDLE             Handle;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  UINT32                 BlockSize;
  // The partition a formatter writes, in blocks.
  UINT64 First;
  UINT64 Blocks;
} DISK;

STATIC VOID
CardFree (IN OUT CARD *Card)
{
  for (UINTN Index = 0; Index < Card->Nodes.Count; Index++) {
    FreePool ((VOID *)Card->Nodes.Items[Index].Name);
    FreePool (Card->Paths.Items[Index]);
  }
  VecFree (&Card->Nodes);
  VecFree (&Card->Paths);
}

STATIC BOOLEAN
Skipped (IN CONST CHAR8 *Name)
{
  if (Name[0] == '.' && Name[1] == '_') {
    return TRUE;
  }
  for (UINTN Index = 0; Index < sizeof (mSkipped) / sizeof (*mSkipped);
       Index++) {
    if (AsciiStrEq (Name, mSkipped[Index])) {
      return TRUE;
    }
  }
  return FALSE;
}

// Adds what directory Dir ("" for the root) holds, depth first, so every
// directory comes before its contents.
STATIC BOOLEAN
ListDir (
  IN OUT CARD    *Card,
  IN CONST CHAR8 *Dir,
  IN INTN         Parent,
  IN UINTN        Depth
)
{
  FILES_DIR_ENTRIES Entries = {0};
  BOOLEAN           Ok      = TRUE;

  FilesList (Card->Fs, Dir[0] ? Dir : "\\", &Entries);
  for (UINTN Index = 0; Ok && Index < Entries.Count; Index++) {
    CONST FILES_DIR_ENTRY *Entry = &Entries.Items[Index];
    FAT32_NODE             Node  = {0};
    CHAR8                 *Path;

    if (Skipped (Entry->Name)) {
      continue;
    }
    Path        = SPrintAlloc ("%s\\%s", Dir, Entry->Name);
    Node.Name   = Utf16FromStr (Entry->Name);
    Node.Parent = Parent;
    Node.Dir    = Entry->IsDir;
    Node.Date   = Card->Date;
    Node.Time   = Card->Time;
    // Nodes and Paths stay the same length.
    if (!Path || !Node.Name || Card->Nodes.Count >= MAX_FILES ||
        (!Node.Dir && !FilesSize (Card->Fs, Path, &Node.Size)) ||
        !VecPush (&Card->Paths, Path)) {
      Ok = FALSE;
    } else if (!VecPush (&Card->Nodes, Node)) {
      Card->Paths.Count--;
      Ok = FALSE;
    }
    if (!Ok) {
      FreePool ((VOID *)Node.Name);
      FreePool (Path);
      break;
    }
    Card->Bytes += Node.Size;
    // A folder too deep fails the listing rather than going without its
    // contents.
    if (Node.Dir) {
      Ok = Depth < MAX_DEPTH &&
           ListDir (Card, Path, (INTN)Card->Nodes.Count - 1, Depth + 1);
    }
  }
  FilesEntriesFree (&Entries);
  return Ok;
}

// The FAT date and time of now, or 2026-01-01 if the clock can't be read
// or isn't set: until an OS sets it, the Nova's clock counts from January
// 1980. Seconds gets the time since 1970.
STATIC VOID
Now (
  OUT UINT16 *Date,
  OUT UINT16 *Time,
  OUT UINT32 *Seconds
)
{
  EFI_TIME Clock;
  UINT32   Year;
  UINT32   Month;
  UINT64   Days;

  if (gRT->GetTime (&Clock, NULL) != EFI_SUCCESS || Clock.Year < 2026 ||
      Clock.Month < 1 || Clock.Month > 12) {
    Clock = (EFI_TIME){.Year = 2026, .Month = 1, .Day = 1};
  }
  *Date = (UINT16)(((Clock.Year - 1980) << 9) | (Clock.Month << 5) | Clock.Day);
  *Time =
    (UINT16)((Clock.Hour << 11) | (Clock.Minute << 5) | (Clock.Second / 2));
  // Days since 1970 from the civil date, March-based.
  Year  = Clock.Year - (Clock.Month <= 2);
  Month = Clock.Month <= 2 ? Clock.Month + 9 : Clock.Month - 3;
  Days  = (UINT64)Year * 365 + Year / 4 - Year / 100 + Year / 400 +
         (153 * Month + 2) / 5 + Clock.Day - 1 - 719468;
  *Seconds = (UINT32)(Days * 86400 + Clock.Hour * 3600u + Clock.Minute * 60u +
                      Clock.Second);
}

// The card's PortareOS: an SD card file system labelled PORTAREOS with
// KERNEL and SYSTEM. FALSE if there's none, or its files can't be listed.
STATIC BOOLEAN
FindCard (
  OUT CARD    *Card,
  IN LOG_SINK  Log
)
{
  VOLUME_FS_LIST Volumes = {0};
  UINT32         Seconds;

  ZeroMem (Card, sizeof (*Card));
  VolumesFileSystems (&Volumes);
  for (UINTN Index = 0; Index < Volumes.Count && !Card->Fs; Index++) {
    VOLUME_FS *Volume = &Volumes.Items[Index];
    CHAR8     *Label;

    if (!Volume->Medium.External || Volume->Medium.Usb) {
      continue;
    }
    Label = FilesVolumeLabel (Volume->Handle);
    if (Label && AsciiStrEq (Label, LABEL) &&
        FilesExists (Volume->Handle, "\\KERNEL") &&
        FilesExists (Volume->Handle, "\\SYSTEM")) {
      Card->Fs = Volume->Handle;
    }
    FreePool (Label);
  }
  VecFree (&Volumes);
  if (!Card->Fs) {
    return FALSE;
  }
  Now (&Card->Date, &Card->Time, &Seconds);
  if (!ListDir (Card, "", -1, 0)) {
    SAY ("the card's files couldn't be listed");
    CardFree (Card);
    Card->Fs = NULL;
    return FALSE;
  }
  return TRUE;
}

// Whether Handle's device path starts with Prefix's, end node aside.
STATIC BOOLEAN
PathUnder (
  IN EFI_HANDLE Handle,
  IN EFI_HANDLE Prefix
)
{
  OABL_BUFFER Path  = {0};
  OABL_BUFFER Start = {0};
  BOOLEAN     Under = FALSE;

  if (PathsHandlePath (Handle, &Path) && PathsHandlePath (Prefix, &Start) &&
      Start.Len >= 4) {
    Under = Path.Len >= Start.Len - 4 &&
            CompareMem (Path.Data, Start.Data, Start.Len - 4) == 0;
  }
  BufferFree (&Path);
  BufferFree (&Start);
  return Under;
}

// The internal disk: the one fixed disk whose checked table has Android or
// PortareOS. FALSE if there's none, or more than one.
STATIC BOOLEAN
FindDisk (
  OUT DISK       *Disk,
  OUT GPT_SURVEY *Survey,
  IN LOG_SINK     Log
)
{
  EFI_HANDLE *Handles;
  UINTN       Count = LocateHandles (&gEfiBlockIoProtocolGuid, &Handles);
  UINTN       Found = 0;

  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL *BlockIo = DiskBlockIo (Handles[Index]);
    OABL_BUFFER            Block   = {0};
    OABL_BUFFER            Array   = {0};
    GPT_HEADER             Header;
    GPT_HEADER             Backup;
    GPT_SURVEY             This;

    if (!BlockIo ||
        !DiskCheckedTable (BlockIo, &Block, &Header, &Array, &Backup)) {
      continue;
    }
    GptSurvey (&Header, Array.Data, Array.Len, &This);
    if (This.Android || This.PortareOs) {
      if (Found++ == 0) {
        *Disk   = (DISK){
          .Handle    = Handles[Index],
          .BlockIo   = BlockIo,
          .BlockSize = BlockIo->Media->BlockSize};
        *Survey = This;
      }
    }
    BufferFree (&Block);
    BufferFree (&Array);
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  if (Found > 1) {
    SAY ("%llu disks hold Android or PortareOS", (UINT64)Found);
  }
  return Found == 1;
}

// InstallInternal's answer: 0 before it looked, 1 for none, 2 for found.
STATIC UINT8      mInternal;
STATIC GPT_SURVEY mInternalSurvey;

BOOLEAN
InstallInternal (OUT GPT_SURVEY *Out)
{
  DISK Disk;

  if (!mInternal) {
    mInternal = FindDisk (&Disk, &mInternalSurvey, LOG_QUIET) ? 2 : 1;
  }
  *Out = mInternalSurvey;
  return mInternal == 2;
}

VOID
InstallSurvey (
  OUT INSTALL_SURVEY *Out,
  IN LOG_SINK         Log
)
{
  CARD Card;
  DISK Disk;

  ZeroMem (Out, sizeof (*Out));
  if (FindCard (&Card, Log)) {
    Out->Card      = TRUE;
    Out->CardBytes = Card.Bytes;
    CardFree (&Card);
  }
  Out->Internal = FindDisk (&Disk, &Out->Gpt, Log);
  if (Out->Internal) {
    Out->BlockSize = Disk.BlockSize;
  }
  SAY (
    "card %s (%llu MiB), internal %s, Android %s, PortareOS %s",
    Out->Card ? "found" : "not found", Out->CardBytes / MIB,
    Out->Internal ? "read" : "not read", Out->Gpt.Android ? "yes" : "no",
    Out->Gpt.PortareOs ? "yes" : "no"
  );
}

BOOLEAN
InstallKeepsStorage (IN CONST INSTALL_SURVEY *Survey)
{
  return Survey->Internal && Survey->Gpt.Android && Survey->Gpt.PortareOs;
}

BOOLEAN
InstallAndroidRange (
  IN CONST INSTALL_SURVEY *Survey,
  OUT UINT64              *Min,
  OUT UINT64              *Max
)
{
  UINT64 Userdata;

  if (!Survey->Internal || !Survey->Gpt.CanShrink) {
    return FALSE;
  }
  Userdata = (Survey->Gpt.UserdataLast - Survey->Gpt.UserdataFirst + 1) *
             Survey->BlockSize;
  if (Userdata < MIN_ANDROID + ALIGN_BYTES + BOOT_BYTES + MIN_STORAGE) {
    return FALSE;
  }
  *Min = MIN_ANDROID;
  *Max = (Userdata - ALIGN_BYTES - BOOT_BYTES - MIN_STORAGE) / GIB * GIB;
  return *Max >= *Min;
}

BOOLEAN
InstallCan (
  IN CONST INSTALL_SURVEY *Survey,
  IN INSTALL_KIND          Kind
)
{
  UINT64 Min;
  UINT64 Max;

  if (Kind == INSTALL_REPLACE_ANDROID) {
    return Survey->Internal && Survey->Gpt.CanReplace &&
           InstallStorageBytes (Survey, Kind, 0) >= MIN_STORAGE;
  }
  return InstallKeepsStorage (Survey) ||
         InstallAndroidRange (Survey, &Min, &Max);
}

UINT64
InstallStorageBytes (
  IN CONST INSTALL_SURVEY *Survey,
  IN INSTALL_KIND          Kind,
  IN UINT64                AndroidBytes
)
{
  UINT64 Align = ALIGN_BYTES / Survey->BlockSize;
  UINT64 First;

  if (!Survey->Internal) {
    return 0;
  }
  if (Kind == INSTALL_REPLACE_ANDROID ? !Survey->Gpt.CanReplace
                                      : !Survey->Gpt.CanShrink &&
                                          !InstallKeepsStorage (Survey)) {
    return 0;
  }
  if (Kind == INSTALL_DUALBOOT && InstallKeepsStorage (Survey)) {
    return (Survey->Gpt.Found.StorageLast - Survey->Gpt.Found.StorageFirst +
            1) *
           Survey->BlockSize;
  }
  First = Kind == INSTALL_DUALBOOT
            ? Survey->Gpt.UserdataFirst + AndroidBytes / Survey->BlockSize
            : Survey->Gpt.ReplaceFirst;
  First = (First + Align - 1) / Align * Align + BOOT_BYTES / Survey->BlockSize;
  return First > Survey->Gpt.LastUsable
           ? 0
           : (Survey->Gpt.LastUsable - First + 1) * Survey->BlockSize;
}

// Random bytes from Qualcomm's RNG, or the clock mixed if it has none:
// partition GUIDs and the file systems' IDs only need to be unique.
STATIC VOID
Random (
  OUT UINT8 *Out,
  IN UINTN   Size
)
{
  EFI_QCOM_RNG_PROTOCOL *Rng = LocateProtocol (&gQcomRngProtocolGuid);
  UINT64                 State;

  if (Rng && Rng->GetRNG (Rng, &gEfiRNGAlgRawGuid, Size, Out) == EFI_SUCCESS) {
    return;
  }
  State = ClockNowUs () ^ ((UINT64)(UINTN)Out << 20);
  for (UINTN Index = 0; Index < Size; Index++) {
    // splitmix64
    UINT64 Mixed = (State += 0x9E3779B97F4A7C15ull);
    Mixed        = (Mixed ^ (Mixed >> 30)) * 0xBF58476D1CE4E5B9ull;
    Mixed        = (Mixed ^ (Mixed >> 27)) * 0x94D049BB133111EBull;
    Out[Index]   = (UINT8)(Mixed ^ (Mixed >> 31));
  }
}

// A random version 4 GUID, laid out as GPT stores it.
STATIC VOID
RandomGuid (OUT UINT8 Guid[16])
{
  Random (Guid, 16);
  Guid[7] = (UINT8)((Guid[7] & 0x0F) | 0x40);
  Guid[8] = (UINT8)((Guid[8] & 0x3F) | 0x80);
}

// Page-aligned memory, which suits any IoAlign.
STATIC UINT8 *
PagesAlloc (IN UINTN Size)
{
  EFI_PHYSICAL_ADDRESS Address;

  return gBS->AllocatePages (
           AllocateAnyPages, EfiLoaderData, EFI_SIZE_TO_PAGES (Size), &Address
         ) == EFI_SUCCESS
           ? (UINT8 *)(UINTN)Address
           : NULL;
}

STATIC VOID
PagesFree (
  IN UINT8 *Buffer,
  IN UINTN  Size
)
{
  if (Buffer) {
    gBS->FreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)Buffer, EFI_SIZE_TO_PAGES (Size));
  }
}

// Writes Bytes of zeros from Lba.
STATIC EFI_STATUS
Zero (
  IN CONST DISK *Disk,
  IN UINT64      Lba,
  IN UINT64      Bytes
)
{
  UINT8     *Buffer = PagesAlloc (CHUNK);
  EFI_STATUS Status = Buffer ? EFI_SUCCESS : EFI_OUT_OF_RESOURCES;

  if (Buffer) {
    ZeroMem (Buffer, CHUNK);
  }
  for (UINT64 At = 0; Status == EFI_SUCCESS && At < Bytes; At += CHUNK) {
    UINTN Size = Bytes - At < CHUNK ? (UINTN)(Bytes - At) : CHUNK;
    Status     = Disk->BlockIo->WriteBlocks (
      Disk->BlockIo, Disk->BlockIo->Media->MediaId,
      Lba + At / Disk->BlockSize, Size, Buffer
    );
  }
  PagesFree (Buffer, CHUNK);
  return Status;
}

// Erases a partition the way the ABL's fastboot erase does: all of it,
// through its own handle, so the flash knows its blocks are free. The
// handle must have the entry's GUID and size, as the table had them when
// the firmware read it. Erased blocks needn't read back as zeros, so
// starts that matter are zeroed after. A partition or erase that's missing
// is only logged.
STATIC VOID
ErasePart (
  IN CONST UINT8  Guid[16],
  IN UINT64       Blocks,
  IN CONST CHAR8 *Name,
  IN LOG_SINK     Log
)
{
  EFI_HANDLE               *Handles;
  UINTN                     Count   = LocateHandles (&gEfiBlockIoProtocolGuid, &Handles);
  UINTN                     Matches = 0;
  EFI_BLOCK_IO_PROTOCOL    *Found   = NULL;
  EFI_ERASE_BLOCK_PROTOCOL *Erase   = NULL;
  EFI_ERASE_BLOCK_TOKEN     Token   = {0};
  EFI_STATUS                Status;

  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL *BlockIo = DiskBlockIo (Handles[Index]);
    OABL_BUFFER            Path    = {0};
    UINT8                  Part[16];

    if (BlockIo && BlockIo->Media->LogicalPartition &&
        PathsHandlePath (Handles[Index], &Path) &&
        UefiPartGuid (Path.Data, Path.Len, Part) &&
        CompareMem (Part, Guid, 16) == 0) {
      Matches++;
      if (BlockIo->Media->LastBlock + 1 == Blocks) {
        Found = BlockIo;
        Erase = GetProtocol (Handles[Index], &gEfiEraseBlockProtocolGuid);
      }
    }
    BufferFree (&Path);
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  if (Matches != 1 || !Found || !Erase) {
    SAY (
      "%s: not erased (%llu with its GUID, %s)", Name, (UINT64)Matches,
      !Found ? "none of its size" : !Erase ? "no erase protocol" : "ambiguous"
    );
    return;
  }
  Status = Erase->EraseBlocks (
    Found, Found->Media->MediaId, 0, &Token,
    (UINTN)(Blocks * Found->Media->BlockSize)
  );
  SAY (
    "%s: %s", Name,
    Status == EFI_SUCCESS ? "erased"
                          : StatusText (Status, (CHAR8[STATUS_TEXT_SIZE]){0})
  );
}

// Erases what the install replaces, from the table as it is now: userdata
// for a first Dualboot; Android's and PortareOS's partitions for Replace
// Android; for an update PortareOS's boot partition, and storage too when
// it gets formatted.
STATIC VOID
EraseOld (
  IN CONST DISK *Disk,
  IN INSTALL_KIND Kind,
  IN BOOLEAN      Update,
  IN BOOLEAN      Format,
  IN LOG_SINK     Log
)
{
  STATIC CONST CHAR8 *CONST Replace[] = {
    "super",  "vbmeta_system_a", "vbmeta_system_b",
    "userdata", GPT_BOOT_NAME,   GPT_STORAGE_NAME};
  STATIC CONST CHAR8 *CONST First[]   = {"userdata"};
  STATIC CONST CHAR8 *CONST Again[]   = {GPT_BOOT_NAME, GPT_STORAGE_NAME};
  CONST CHAR8 *CONST       *Names     = Kind == INSTALL_REPLACE_ANDROID ? Replace
                                        : Update                        ? Again
                                                                        : First;
  UINTN Count = Kind == INSTALL_REPLACE_ANDROID ? 6 : Update ? (Format ? 2 : 1)
                                                             : 1;
  OABL_BUFFER Block = {0};
  OABL_BUFFER Array = {0};
  GPT_HEADER  Header;
  GPT_HEADER  Backup;

  if (!DiskCheckedTable (Disk->BlockIo, &Block, &Header, &Array, &Backup)) {
    return;
  }
  for (UINTN Index = 0; Index < Header.Entries; Index++) {
    for (UINTN Name = 0; Name < Count; Name++) {
      UINT8  Guid[16];
      UINT64 FirstBlock;
      UINT64 Last;

      if (GptEntryNamed (&Header, Array.Data, Array.Len, Index, Names[Name]) &&
          GptEntryGuid (&Header, Array.Data, Array.Len, Index, Guid) &&
          GptExtent (&Header, Array.Data, Array.Len, Index, &FirstBlock, &Last) &&
          Last >= FirstBlock) {
        ErasePart (Guid, Last - FirstBlock + 1, Names[Name], Log);
      }
    }
  }
  BufferFree (&Block);
  BufferFree (&Array);
}

// The formatters' writes, at an offset in the partition Disk->First and
// Disk->Blocks give.
STATIC BOOLEAN
PartWrite (
  IN UINT64       Offset,
  IN CONST UINT8 *Buffer,
  IN UINTN        Size,
  IN VOID        *Context
)
{
  CONST DISK *Disk = Context;

  if (Offset % Disk->BlockSize || Size % Disk->BlockSize ||
      (Offset + Size) / Disk->BlockSize > Disk->Blocks) {
    return FALSE;
  }
  return DiskWrite (
           Disk->BlockIo, Disk->First + Offset / Disk->BlockSize, Buffer, Size
         ) == EFI_SUCCESS;
}

STATIC BOOLEAN
PartRead (
  IN UINT64  Offset,
  OUT UINT8 *Buffer,
  IN UINTN   Size,
  IN VOID   *Context
)
{
  CONST DISK *Disk  = Context;
  UINT64      Block = Offset / Disk->BlockSize;
  UINTN       Skip  = (UINTN)(Offset % Disk->BlockSize);
  UINTN       Whole = (Skip + Size + Disk->BlockSize - 1) / Disk->BlockSize *
                Disk->BlockSize;
  OABL_BUFFER Data = {0};
  BOOLEAN     Ok;

  Ok = DiskRead (Disk->BlockIo, Disk->First + Block, Whole, &Data);
  if (Ok) {
    CopyMem (Buffer, Data.Data + Skip, Size);
  }
  BufferFree (&Data);
  return Ok;
}

// Progress across the steps: preparing to 5%, storage to 10%, copying to
// 70%, reading back to 100%.
typedef struct {
  INSTALL_PROGRESS Fn;
  VOID            *Context;
  UINT64           Bytes;
} PROGRESS;

STATIC VOID
Report (
  IN CONST PROGRESS *Progress,
  IN INSTALL_STEP    Step,
  IN UINT64          Done
)
{
  STATIC CONST UINT32 From[] = {0, 50, 100, 700};
  STATIC CONST UINT32 To[]   = {50, 100, 700, 1000};
  UINT64              Share  = Progress->Bytes ? Done * 1000 / Progress->Bytes
                                               : 0;

  if (Progress->Fn) {
    Progress->Fn (
      Step,
      From[Step] + (UINT32)((To[Step] - From[Step]) * (Share > 1000 ? 1000 : Share) / 1000),
      Progress->Context
    );
  }
}

// Reads Want bytes of a card file from Offset, through XBL's FAT driver,
// or straight off FAT32 if that comes up short.
STATIC BOOLEAN
CardRead (
  IN CONST CARD  *Card,
  IN CONST CHAR8 *Path,
  IN UINT64       Offset,
  OUT UINT8      *Buffer,
  IN UINTN        Want
)
{
  OABL_BUFFER Raw = {0};
  UINTN       Got = 0;
  BOOLEAN     Ok;

  if (FilesReadAt (Card->Fs, Path, Offset, Buffer, Want, &Got) && Got == Want) {
    return TRUE;
  }
  Ok = RawFatReadRange (Card->Fs, Path, Offset, Want, &Raw) && Raw.Len == Want;
  if (Ok) {
    CopyMem (Buffer, Raw.Data, Want);
  }
  BufferFree (&Raw);
  return Ok;
}

// Copies the card's files to where Plan put them in the boot partition at
// Disk->First, then reads them back. Each chunk's CRC is kept from the copy
// for the check.
STATIC EFI_STATUS
CopyFiles (
  IN CONST CARD       *Card,
  IN CONST FAT32_PLAN *Plan,
  IN CONST DISK       *Disk,
  IN CONST PROGRESS   *Progress,
  IN LOG_SINK          Log
)
{
  UINT8     *Buffer = PagesAlloc (CHUNK);
  UINT32    *Crcs;
  UINTN      Chunks = 0;
  EFI_STATUS Status = EFI_SUCCESS;

  for (UINTN Index = 0; Index < Card->Nodes.Count; Index++) {
    Chunks += (UINTN)((Card->Nodes.Items[Index].Size + CHUNK - 1) / CHUNK);
  }
  Crcs = AllocateZeroArray (Chunks ? Chunks : 1, sizeof (UINT32));
  if (!Buffer || !Crcs) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  for (UINT32 Pass = 0; Pass < 2 && Status == EFI_SUCCESS; Pass++) {
    UINTN  Chunk = 0;
    UINT64 Done  = 0;

    for (UINTN Index = 0; Index < Card->Nodes.Count; Index++) {
      CONST FAT32_NODE *Node = &Card->Nodes.Items[Index];
      UINT64            Lba;

      if (Node->Dir || Node->Size == 0) {
        continue;
      }
      Lba = Disk->First +
            Fat32MakeOffset (Plan, Node->Cluster) / Disk->BlockSize;
      for (UINT64 At = 0; At < Node->Size; At += CHUNK, Chunk++) {
        UINTN Want = Node->Size - At < CHUNK ? (UINTN)(Node->Size - At) : CHUNK;
        // Whole blocks; the last one's tail is zero.
        UINTN Size = (Want + Disk->BlockSize - 1) / Disk->BlockSize *
                     Disk->BlockSize;
        UINT64 Block = Lba + At / Disk->BlockSize;

        if (Pass == 0) {
          if (!CardRead (Card, Card->Paths.Items[Index], At, Buffer, Want)) {
            SAY ("reading %s at %llu failed", Card->Paths.Items[Index], At);
            Status = EFI_DEVICE_ERROR;
            goto Done;
          }
          ZeroMem (Buffer + Want, Size - Want);
          Crcs[Chunk] = CalculateCrc32 (Buffer, Size);
          Status      = Disk->BlockIo->WriteBlocks (
            Disk->BlockIo, Disk->BlockIo->Media->MediaId, Block, Size, Buffer
          );
        } else {
          Status = Disk->BlockIo->ReadBlocks (
            Disk->BlockIo, Disk->BlockIo->Media->MediaId, Block, Size, Buffer
          );
          if (Status == EFI_SUCCESS &&
              CalculateCrc32 (Buffer, Size) != Crcs[Chunk]) {
            SAY ("%s at %llu read back different", Card->Paths.Items[Index], At);
            Status = EFI_CRC_ERROR;
          }
        }
        if (Status != EFI_SUCCESS) {
          goto Done;
        }
        Done += Want;
        Report (Progress, Pass == 0 ? INSTALL_STEP_COPY : INSTALL_STEP_CHECK, Done);
      }
    }
    if (Pass == 0) {
      Status = Disk->BlockIo->FlushBlocks (Disk->BlockIo);
    }
  }
Done:
  PagesFree (Buffer, CHUNK);
  FreePool (Crcs);
  return Status;
}

// Whether the partition at Disk->First holds an ext4 superblock.
STATIC BOOLEAN
HasExt4 (IN CONST DISK *Disk)
{
  UINT8 Magic[2];

  return PartRead (1024 + 0x38, Magic, 2, (VOID *)Disk) && Magic[0] == 0x53 &&
         Magic[1] == 0xEF;
}

// Writes the new table, then reads it back: PortareOS must be where Where
// says.
STATIC EFI_STATUS
WriteTable (
  IN CONST DISK          *Disk,
  IN CONST GPT_WRITES    *Writes,
  IN CONST GPT_PORTAREOS *Where,
  IN LOG_SINK             Log
)
{
  OABL_BUFFER Block = {0};
  OABL_BUFFER Array = {0};
  GPT_HEADER  Header;
  GPT_HEADER  Backup;
  GPT_SURVEY  Survey;
  EFI_STATUS  Status = EFI_SUCCESS;

  // Flushed one by one, so the order Gpt chose reaches the disk.
  for (UINTN Index = 0; Index < Writes->Count && Status == EFI_SUCCESS;
       Index++) {
    Status = DiskWrite (
      Disk->BlockIo, Writes->Items[Index].Lba, Writes->Items[Index].Data.Data,
      Writes->Items[Index].Data.Len
    );
    if (Status == EFI_SUCCESS) {
      Status = Disk->BlockIo->FlushBlocks (Disk->BlockIo);
    }
  }
  if (Status != EFI_SUCCESS) {
    SAY ("writing the table failed");
    return Status;
  }
  if (!DiskCheckedTable (Disk->BlockIo, &Block, &Header, &Array, &Backup)) {
    SAY ("the table didn't read back");
    return EFI_CRC_ERROR;
  }
  GptSurvey (&Header, Array.Data, Array.Len, &Survey);
  BufferFree (&Block);
  BufferFree (&Array);
  if (!Survey.PortareOs || Survey.Found.BootFirst != Where->BootFirst ||
      Survey.Found.StorageLast != Where->StorageLast ||
      CompareMem (Survey.Found.BootGuid, Where->BootGuid, 16) != 0) {
    SAY ("the table read back without PortareOS where it was put");
    return EFI_CRC_ERROR;
  }
  return EFI_SUCCESS;
}

// Whether the battery is charged enough to write, by the firmware's own
// test for flashing, as the ABL's fastboot asks it. TRUE where the firmware
// can't tell (no battery, or an older protocol).
STATIC BOOLEAN
PowerOk (IN LOG_SINK Log)
{
  EFI_CHARGER_EX_PROTOCOL  *Charger = LocateProtocol (&gChargerExProtocolGuid);
  EFI_CHARGER_EX_FLASH_INFO Flash   = {0};
  EFI_STATUS                Status;

  if (!Charger || Charger->Revision < CHARGER_EX_REVISION) {
    return TRUE;
  }
  Status = Charger->IsPowerOk (
    EFI_CHARGER_EX_POWER_FLASH_BATTERY_VOLTAGE_TYPE, &Flash
  );
  if (Status == EFI_UNSUPPORTED) {
    return TRUE;
  }
  SAY (
    "battery %u mV, %u mV needed%s", Flash.BattCurrVoltage,
    Flash.BattRequiredVoltage, Flash.bCanFlash ? "" : ", too low to flash"
  );
  return Status == EFI_SUCCESS && Flash.bCanFlash &&
         Flash.BattCurrVoltage >= Flash.BattRequiredVoltage;
}

// The new table's writes, or none to keep the layout. Where gets PortareOS's
// partitions.
STATIC EFI_STATUS
NewTable (
  IN CONST DISK     *Disk,
  IN INSTALL_KIND    Kind,
  IN UINT64          AndroidBytes,
  OUT GPT_WRITES    *Writes,
  OUT GPT_PORTAREOS *Where,
  OUT GPT_SURVEY    *Survey,
  IN LOG_SINK        Log
)
{
  OABL_BUFFER Block   = {0};
  OABL_BUFFER Array   = {0};
  GPT_INSTALL Install = {0};
  GPT_HEADER  Header;
  GPT_HEADER  Backup;
  BOOLEAN     Ok;

  if (!DiskCheckedTable (Disk->BlockIo, &Block, &Header, &Array, &Backup)) {
    return EFI_VOLUME_CORRUPTED;
  }
  GptSurvey (&Header, Array.Data, Array.Len, Survey);
  if (Kind == INSTALL_DUALBOOT && Survey->Android && Survey->PortareOs) {
    *Where = Survey->Found;
    Ok     = TRUE;
  } else {
    Install.AndroidBlocks = AndroidBytes / Disk->BlockSize;
    Install.BootBlocks    = BOOT_BYTES / Disk->BlockSize;
    Install.MinStorage    = MIN_STORAGE / Disk->BlockSize;
    Install.Align         = ALIGN_BYTES / Disk->BlockSize;
    RandomGuid (Install.BootGuid);
    RandomGuid (Install.StorageGuid);
    Ok = Kind == INSTALL_DUALBOOT
           ? AndroidBytes >= MIN_ANDROID &&
               GptInstallDualboot (
                 Block.Data, Block.Len, Array.Data, Array.Len,
                 Disk->BlockSize, Backup.EntryLba, &Install, Writes, Where
               )
           : GptInstallReplace (
               Block.Data, Block.Len, Array.Data, Array.Len, Disk->BlockSize,
               Backup.EntryLba, &Install, Writes, Where
             );
  }
  BufferFree (&Block);
  BufferFree (&Array);
  if (!Ok) {
    SAY ("the table doesn't allow this install");
    return EFI_VOLUME_CORRUPTED;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
InstallRun (
  IN INSTALL_KIND     Kind,
  IN UINT64           AndroidBytes,
  IN INSTALL_PROGRESS Progress,
  IN VOID            *Context,
  IN LOG_SINK         Log
)
{
  CARD          Card;
  DISK          Disk;
  GPT_SURVEY    Survey;
  GPT_SURVEY    Before;
  GPT_WRITES    Writes = {0};
  GPT_PORTAREOS Where;
  FAT32_PLAN    Plan = {0};
  EXT4_MAKE     Ext4 = {0};
  FAT32_VOLUME  Volume;
  FAT32_FILE    Kernel;
  PROGRESS      Steps  = {Progress, Context, 1};
  BOOLEAN       Format;
  BOOLEAN       Shrink;
  UINT32        Seconds;
  UINT64        KernelSize = 0;
  EFI_STATUS    Status;

  // Whatever happens, the disk is looked at again next time.
  mInternal = 0;
  if (!FindCard (&Card, Log)) {
    SAY ("no PortareOS card");
    return EFI_NOT_FOUND;
  }
  if (!FindDisk (&Disk, &Before, Log) || PathUnder (Card.Fs, Disk.Handle)) {
    CardFree (&Card);
    return EFI_VOLUME_CORRUPTED;
  }
  if (!PowerOk (Log)) {
    CardFree (&Card);
    return EFI_NOT_READY;
  }
  Status = NewTable (&Disk, Kind, AndroidBytes, &Writes, &Where, &Survey, Log);
  if (Status != EFI_SUCCESS) {
    goto Done;
  }
  Shrink = Kind == INSTALL_DUALBOOT && Writes.Count;
  // A reinstall keeps storage, unless it has no file system yet.
  Disk.First  = Where.StorageFirst;
  Disk.Blocks = Where.StorageLast - Where.StorageFirst + 1;
  Format      = Writes.Count || !HasExt4 (&Disk);
  // The card's files laid out in the boot partition, before any write.
  Plan.Bytes         = (Where.BootLast - Where.BootFirst + 1) * Disk.BlockSize;
  Plan.SectorSize    = Disk.BlockSize;
  Plan.HiddenSectors = (UINT32)Where.BootFirst;
  Plan.Nodes         = Card.Nodes.Items;
  Plan.Count         = Card.Nodes.Count;
  Random ((UINT8 *)&Plan.Serial, sizeof (Plan.Serial));
  AsciiStrCopy (Plan.Label, sizeof (Plan.Label), LABEL);
  Now (&Plan.Date, &Plan.Time, &Seconds);
  if (!Fat32MakePlan (&Plan)) {
    SAY ("the card's files don't fit the boot partition");
    Status = EFI_VOLUME_FULL;
    goto Done;
  }
  for (UINTN Index = 0; Index < Card.Nodes.Count; Index++) {
    if (AsciiStrEq (Card.Paths.Items[Index], "\\KERNEL")) {
      KernelSize = Card.Nodes.Items[Index].Size;
    }
  }
  Steps.Bytes = Card.Bytes;
  SAY (
    "%s: boot partition at block %llu, storage %llu GiB%s",
    Kind == INSTALL_DUALBOOT ? "Dualboot" : "Replace Android", Where.BootFirst,
    (Where.StorageLast - Where.StorageFirst + 1) * Disk.BlockSize / GIB,
    Format ? ", formatted" : ", kept"
  );
  Report (&Steps, INSTALL_STEP_PREPARE, 0);
  // From here on the disk changes: first what's replaced is erased, while
  // the firmware still has its partitions as they were.
  EraseOld (&Disk, Kind, Writes.Count == 0, Format, Log);
  if (Writes.Count) {
    Status = WriteTable (&Disk, &Writes, &Where, Log);
    if (Status != EFI_SUCCESS) {
      goto Done;
    }
  }
  if (Shrink) {
    // Android formats a userdata it finds blank on its next start.
    Status = Zero (&Disk, Survey.UserdataFirst, USERDATA_ZERO);
    if (Status != EFI_SUCCESS) {
      SAY ("zeroing userdata failed");
      goto Done;
    }
  }
  Status = Zero (&Disk, Where.BootFirst, HEAD_ZERO);
  if (Status == EFI_SUCCESS && Format) {
    Status = Zero (&Disk, Where.StorageFirst, HEAD_ZERO);
  }
  if (Status != EFI_SUCCESS) {
    SAY ("clearing the partitions failed");
    goto Done;
  }
  Report (&Steps, INSTALL_STEP_STORAGE, 0);
  if (Format) {
    Ext4.Bytes         = Disk.Blocks * Disk.BlockSize;
    Ext4.InodeRatio    = INODE_RATIO;
    Ext4.JournalBlocks = JOURNAL_BLOCKS;
    RandomGuid (Ext4.Uuid);
    Random (Ext4.HashSeed, sizeof (Ext4.HashSeed));
    AsciiStrCopy (Ext4.Label, sizeof (Ext4.Label), GPT_STORAGE_NAME);
    Ext4.Time = Seconds;
    if (!Ext4Make (&Ext4, PartWrite, &Disk)) {
      SAY ("formatting storage failed");
      Status = EFI_DEVICE_ERROR;
      goto Done;
    }
  }
  // The files, then the FATs and boot sector, then the volume read back as
  // the menu will.
  Disk.First  = Where.BootFirst;
  Disk.Blocks = Where.BootLast - Where.BootFirst + 1;
  Status      = CopyFiles (&Card, &Plan, &Disk, &Steps, Log);
  if (Status != EFI_SUCCESS) {
    goto Done;
  }
  if (!Fat32MakeWrite (&Plan, PartWrite, &Disk) ||
      Disk.BlockIo->FlushBlocks (Disk.BlockIo) != EFI_SUCCESS) {
    SAY ("writing the boot partition's FAT failed");
    Status = EFI_DEVICE_ERROR;
    goto Done;
  }
  if (!Fat32Mount (&Volume, Disk.BlockSize, PartRead, &Disk)) {
    SAY ("the boot partition doesn't read back as FAT32");
    Status = EFI_CRC_ERROR;
    goto Done;
  }
  if (!Fat32Find (&Volume, "KERNEL", &Kernel) || Kernel.Size != KernelSize) {
    SAY ("KERNEL doesn't read back from the boot partition");
    Status = EFI_CRC_ERROR;
  }
  Fat32Free (&Volume);
  if (Status == EFI_SUCCESS) {
    SAY ("installed: %llu MiB copied and read back", Card.Bytes / MIB);
  }
Done:
  GptWritesFree (&Writes);
  CardFree (&Card);
  return Status;
}
