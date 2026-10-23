/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Files - file access, device paths, volumes and raw FAT32 reads.
*/

#include "Efi/Files.h"

#include "Base/Memory.h"
#include "Base/Text.h"
#include "Core/EfiData.h"
#include "Core/Fat32.h"

// Largest single Read request.
#define CHUNK ((UINTN)1 << 20)

// Converts Text to a terminated UCS-2 string. Returns NULL for a character
// outside UCS-2 or when out of memory. Caller frees it.
STATIC CHAR16 *
Ucs2 (IN CONST CHAR8 *Text)
{
  UINTN   Count;
  UINT16 *Units = Utf16FromText (TextOf (Text), &Count);

  if (!Units) {
    return NULL;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    if (Units[Index] >= 0xD800 && Units[Index] <= 0xDFFF) {
      FreePool (Units);
      return NULL;
    }
  }
  return (CHAR16 *)Units;
}

// Opens the volume's root. File handles stay valid without the protocol.
STATIC EFI_FILE_PROTOCOL *
Root (IN EFI_HANDLE Fs)
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem =
    GetProtocol (Fs, &gEfiSimpleFileSystemProtocolGuid);
  EFI_FILE_PROTOCOL *RootDir = NULL;

  if (!FileSystem ||
      FileSystem->OpenVolume (FileSystem, &RootDir) != EFI_SUCCESS) {
    return NULL;
  }
  return RootDir;
}

// Returns 1 for a file, 0 for a directory, -1 on error. GetPosition is
// unsupported on a directory.
STATIC INT32
Kind (IN EFI_FILE_PROTOCOL *File)
{
  UINT64     Position = 0;
  EFI_STATUS Status   = File->GetPosition (File, &Position);

  return Status == EFI_SUCCESS ? 1 : Status == EFI_UNSUPPORTED ? 0 : -1;
}

// Opens Path in Mode. Fails unless it is a regular file.
STATIC EFI_FILE_PROTOCOL *
OpenFile (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR16 *Path,
  IN UINT64        Mode
)
{
  EFI_FILE_PROTOCOL *RootDir = Root (Fs);
  EFI_FILE_PROTOCOL *File    = NULL;

  if (!RootDir) {
    return NULL;
  }
  if (RootDir->Open (RootDir, &File, (CHAR16 *)Path, Mode, 0) != EFI_SUCCESS) {
    File = NULL;
  } else if (Kind (File) != 1) {
    File->Close (File);
    File = NULL;
  }
  RootDir->Close (RootDir);
  return File;
}

STATIC EFI_FILE_PROTOCOL *
OpenPath (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path
)
{
  CHAR16            *WidePath = Ucs2 (Path);
  EFI_FILE_PROTOCOL *File;

  if (!WidePath) {
    return NULL;
  }
  File = OpenFile (Fs, WidePath, EFI_FILE_MODE_READ);
  FreePool (WidePath);
  return File;
}

// Reads Length bytes in requests of at most CHUNK, stopping at a short read.
// Got gets the total.
STATIC BOOLEAN
ReadChunked (
  IN EFI_FILE_PROTOCOL *File,
  OUT UINT8            *Buffer,
  IN UINTN              Length,
  OUT UINTN            *Got
)
{
  UINTN Total = 0;

  while (Length > 0) {
    UINTN Want = Length < CHUNK ? Length : CHUNK;
    UINTN Size = Want;
    if (File->Read (File, &Size, Buffer) != EFI_SUCCESS) {
      return FALSE;
    }
    // Never more than asked for, whatever the firmware says.
    if (Size > Want) {
      Size = Want;
    }
    Total += Size;
    Length -= Size;
    Buffer += Size;
    if (Size < Want) {
      break;
    }
  }
  *Got = Total;
  return TRUE;
}

// Reads into Buffer until it is full or the file ends.
STATIC BOOLEAN
ReadAll (
  IN EFI_FILE_PROTOCOL *File,
  OUT UINT8            *Buffer,
  IN UINTN              Length,
  OUT UINTN            *Got
)
{
  UINTN Done = 0;
  UINTN Count;

  while (Done < Length) {
    if (!ReadChunked (File, Buffer + Done, Length - Done, &Count)) {
      return FALSE;
    }
    if (Count == 0) {
      break;
    }
    Done += Count;
  }
  *Got = Done;
  return TRUE;
}

// Reads up to Length bytes from the current position into a new buffer.
STATIC BOOLEAN
ReadInto (
  IN EFI_FILE_PROTOCOL *File,
  IN UINTN              Length,
  OUT OABL_BUFFER      *Out
)
{
  UINT8 *Buffer = AllocatePool (Length ? Length : 1);
  UINTN  Got;

  if (!Buffer) {
    return FALSE;
  }
  if (!ReadAll (File, Buffer, Length, &Got)) {
    FreePool (Buffer);
    return FALSE;
  }
  *Out = (OABL_BUFFER){Buffer, Got, Length ? Length : 1};
  return TRUE;
}

// Checks that the UCS-2 name at At ends within the Size bytes written.
STATIC BOOLEAN
NameEnds (
  IN CONST UINT8 *Info,
  IN UINTN        Size,
  IN UINTN        At
)
{
  if (Size < At) {
    return FALSE;
  }
  for (UINTN Index = At; Index + 2 <= Size; Index += 2) {
    if (!Info[Index] && !Info[Index + 1]) {
      return TRUE;
    }
  }
  return FALSE;
}

// Gets the info of type Guid into a new buffer, sized by a first call and
// rounded up to Align. Returns EFI_BAD_BUFFER_SIZE if the name at NameAt is
// missing or runs past the data, EFI_BUFFER_TOO_SMALL if the firmware asks
// for no bytes, else the firmware's status.
STATIC EFI_STATUS
GetInfo (
  IN EFI_FILE_PROTOCOL *File,
  IN EFI_GUID          *Guid,
  IN UINTN              NameAt,
  IN UINTN              Align,
  OUT VOID            **Out
)
{
  UINT64     Dummy;
  UINTN      Size = 0;
  UINTN      Capacity;
  UINT8     *Buffer;
  EFI_STATUS Status = File->GetInfo (File, Guid, &Size, &Dummy);

  // Success with no room means there is no name.
  if (Status == EFI_SUCCESS) {
    return EFI_BAD_BUFFER_SIZE;
  }
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return Status;
  }
  if (!Size) {
    return EFI_BUFFER_TOO_SMALL;
  }
  Capacity = AlignUp (Size, Align);
  Buffer   = AllocatePool (Capacity);
  if (!Buffer) {
    return EFI_OUT_OF_RESOURCES;
  }
  Size   = Capacity;
  Status = File->GetInfo (File, Guid, &Size, Buffer);
  if (Status == EFI_SUCCESS &&
      !NameEnds (Buffer, Size < Capacity ? Size : Capacity, NameAt)) {
    Status = EFI_BAD_BUFFER_SIZE;
  }
  if (Status != EFI_SUCCESS) {
    FreePool (Buffer);
    return Status;
  }
  *Out = Buffer;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
FileInfo (
  IN EFI_FILE_PROTOCOL *File,
  OUT EFI_FILE_INFO   **Out
)
{
  return GetInfo (
    File, &gEfiFileInfoGuid, SIZE_OF_EFI_FILE_INFO, 8, (VOID **)Out
  );
}

BOOLEAN
FilesReadRange (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR16 *Path,
  IN UINT64        Offset,
  IN UINTN         Length,
  OUT OABL_BUFFER *Out
)
{
  EFI_FILE_PROTOCOL *File = OpenFile (Fs, Path, EFI_FILE_MODE_READ);
  BOOLEAN            Ok;

  if (!File) {
    return FALSE;
  }
  Ok = File->SetPosition (File, Offset) == EFI_SUCCESS &&
       ReadInto (File, Length, Out);
  File->Close (File);
  return Ok;
}

BOOLEAN
FilesRead (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Path,
  IN UINTN         Limit,
  OUT OABL_BUFFER *Out
)
{
  EFI_FILE_PROTOCOL *File = OpenPath (Fs, Path);
  EFI_FILE_INFO     *Info;
  BOOLEAN            Ok = FALSE;

  if (!File) {
    return FALSE;
  }
  if (FileInfo (File, &Info) == EFI_SUCCESS) {
    UINT64 Size = Info->FileSize;
    FreePool (Info);
    Ok = Size <= Limit && ReadInto (File, (UINTN)Size, Out);
  }
  File->Close (File);
  return Ok;
}

CHAR8 *
FilesReadText (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path,
  IN UINTN        Limit
)
{
  OABL_BUFFER Bytes = {0};
  CHAR8      *Text;

  if (!FilesRead (Fs, Path, Limit, &Bytes)) {
    return NULL;
  }
  Text = Utf8Lossy (Bytes.Data, Bytes.Len);
  BufferFree (&Bytes);
  return Text;
}

BOOLEAN
FilesReadPart (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Path,
  IN UINT64        Offset,
  IN UINTN         Length,
  OUT OABL_BUFFER *Out
)
{
  CHAR16 *WidePath = Ucs2 (Path);
  BOOLEAN Ok;

  if (!WidePath) {
    return FALSE;
  }
  Ok = FilesReadRange (Fs, WidePath, Offset, Length, Out);
  FreePool (WidePath);
  return Ok;
}

BOOLEAN
FilesSize (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path,
  OUT UINT64     *Out
)
{
  EFI_FILE_PROTOCOL *File = OpenPath (Fs, Path);
  EFI_FILE_INFO     *Info;
  BOOLEAN            Ok = FALSE;

  if (!File) {
    return FALSE;
  }
  if (FileInfo (File, &Info) == EFI_SUCCESS) {
    *Out = Info->FileSize;
    FreePool (Info);
    Ok = TRUE;
  }
  File->Close (File);
  return Ok;
}

BOOLEAN
FilesReadTo (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path,
  OUT UINT8      *Buffer,
  IN UINTN        Size,
  OUT UINTN      *Got
)
{
  EFI_FILE_PROTOCOL *File = OpenPath (Fs, Path);
  BOOLEAN            Ok;

  if (!File) {
    return FALSE;
  }
  Ok = ReadAll (File, Buffer, Size, Got);
  File->Close (File);
  return Ok;
}

BOOLEAN
FilesReadAt (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path,
  IN UINT64       Offset,
  OUT UINT8      *Buffer,
  IN UINTN        Size,
  OUT UINTN      *Got
)
{
  EFI_FILE_PROTOCOL *File = OpenPath (Fs, Path);
  BOOLEAN            Ok;

  if (!File) {
    return FALSE;
  }
  Ok = File->SetPosition (File, Offset) == EFI_SUCCESS &&
       ReadAll (File, Buffer, Size, Got);
  File->Close (File);
  return Ok;
}

BOOLEAN
FilesExists (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path
)
{
  EFI_FILE_PROTOCOL *File = OpenPath (Fs, Path);

  if (!File) {
    return FALSE;
  }
  File->Close (File);
  return TRUE;
}

// Reads the next directory entry, or NULL at the end or on error.
STATIC EFI_FILE_INFO *
ReadEntry (IN EFI_FILE_PROTOCOL *Directory)
{
  UINT64     Dummy;
  UINTN      Size = 0;
  UINTN      Capacity;
  UINT8     *Buffer;
  EFI_STATUS Status = Directory->Read (Directory, &Size, &Dummy);

  // A zero-size read means the end of the directory.
  if (Status == EFI_SUCCESS && Size == 0) {
    return NULL;
  }
  // Get the entry's size: an empty read fails with EFI_BUFFER_TOO_SMALL and
  // sets Size.
  Size   = 0;
  Status = Directory->Read (Directory, &Size, &Dummy);
  if (Status != EFI_BUFFER_TOO_SMALL || !Size) {
    return NULL;
  }
  Capacity = AlignUp (Size, 8);
  Buffer   = AllocatePool (Capacity);
  if (!Buffer) {
    return NULL;
  }
  Size   = Capacity;
  Status = Directory->Read (Directory, &Size, Buffer);
  if (Status != EFI_SUCCESS || Size == 0 ||
      !NameEnds (
        Buffer, Size < Capacity ? Size : Capacity, SIZE_OF_EFI_FILE_INFO
      )) {
    FreePool (Buffer);
    return NULL;
  }
  return (EFI_FILE_INFO *)Buffer;
}

VOID
FilesList (
  IN EFI_HANDLE             Fs,
  IN CONST CHAR8           *Dir,
  IN OUT FILES_DIR_ENTRIES *Out
)
{
  CHAR16            *WidePath = Ucs2 (Dir);
  EFI_FILE_PROTOCOL *RootDir;
  EFI_FILE_PROTOCOL *Directory = NULL;
  EFI_FILE_INFO     *Info;

  if (!WidePath) {
    return;
  }
  RootDir = Root (Fs);
  if (RootDir) {
    if (RootDir->Open (RootDir, &Directory, WidePath, EFI_FILE_MODE_READ, 0) !=
        EFI_SUCCESS) {
      Directory = NULL;
    }
    RootDir->Close (RootDir);
  }
  FreePool (WidePath);
  if (!Directory) {
    return;
  }
  if (Kind (Directory) != 0) {
    Directory->Close (Directory);
    return;
  }
  while ((Info = ReadEntry (Directory))) {
    BOOLEAN IsDir = (Info->Attribute & EFI_FILE_DIRECTORY) != 0;
    CHAR8  *Name  = Utf16ToStr (Info->FileName);
    FreePool (Info);
    if (!Name) {
      continue;
    }
    if (AsciiStrEq (Name, ".") || AsciiStrEq (Name, "..") ||
        !VecPush (Out, ((FILES_DIR_ENTRY){Name, IsDir}))) {
      FreePool (Name);
    }
  }
  Directory->Close (Directory);
}

VOID
FilesEntriesFree (IN OUT FILES_DIR_ENTRIES *Entries)
{
  for (UINTN Index = 0; Index < Entries->Count; Index++) {
    FreePool (Entries->Items[Index].Name);
  }
  VecFree (Entries);
}

EFI_STATUS
FilesRename (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path,
  IN CONST CHAR8 *NewName
)
{
  CHAR16            *WidePath = Ucs2 (Path);
  CHAR16            *WideName = Ucs2 (NewName);
  CHAR16            *Name;
  EFI_FILE_PROTOCOL *File;
  EFI_FILE_INFO     *Old;
  EFI_FILE_INFO     *Info;
  UINTN              Units;
  UINTN              Size;
  EFI_STATUS         Status;

  if (!WidePath || !WideName) {
    FreePool (WidePath);
    FreePool (WideName);
    return EFI_INVALID_PARAMETER;
  }
  File = OpenFile (Fs, WidePath, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE);
  FreePool (WidePath);
  if (!File) {
    FreePool (WideName);
    return EFI_NOT_FOUND;
  }
  Status = FileInfo (File, &Old);
  if (Status != EFI_SUCCESS) {
    goto Done;
  }
  // New info: header, name and NUL, zero-padded to a multiple of 8. SetInfo
  // gets the padded size.
  Units = Utf16Len (WideName);
  Size  = AlignUp (SIZE_OF_EFI_FILE_INFO + (Units + 1) * sizeof (CHAR16), 8);
  Info  = AllocateZeroPool (Size);
  if (!Info) {
    FreePool (Old);
    Status = EFI_BUFFER_TOO_SMALL;
    goto Done;
  }
  Info->Size             = Size;
  Info->FileSize         = Old->FileSize;
  Info->PhysicalSize     = Old->PhysicalSize;
  Info->CreateTime       = Old->CreateTime;
  Info->LastAccessTime   = Old->LastAccessTime;
  Info->ModificationTime = Old->ModificationTime;
  Info->Attribute        = Old->Attribute;
  FreePool (Old);
  Name = (CHAR16 *)((UINT8 *)Info + SIZE_OF_EFI_FILE_INFO);
  for (UINTN Index = 0; Index <= Units; Index++) {
    Name[Index] = WideName[Index];
  }
  Status = File->SetInfo (File, &gEfiFileInfoGuid, Size, Info);
  FreePool (Info);
  if (Status == EFI_SUCCESS) {
    Status = File->Flush (File);
  }
Done:
  File->Close (File);
  FreePool (WideName);
  return Status;
}

CHAR8 *
FilesVolumeLabel (IN EFI_HANDLE Fs)
{
  EFI_FILE_PROTOCOL            *RootDir = Root (Fs);
  EFI_FILE_SYSTEM_VOLUME_LABEL *Label;
  OABL_TEXT                     Trimmed;
  CHAR8                        *Text;
  CHAR8                        *Out = NULL;
  EFI_STATUS                    Status;

  if (!RootDir) {
    return NULL;
  }
  Status = GetInfo (
    RootDir, &gEfiFileSystemVolumeLabelInfoIdGuid, 0, sizeof (CHAR16),
    (VOID **)&Label
  );
  RootDir->Close (RootDir);
  if (Status != EFI_SUCCESS) {
    return NULL;
  }
  Text = Utf16ToStr (Label->VolumeLabel);
  FreePool (Label);
  if (!Text) {
    return NULL;
  }
  Trimmed = TextTrim (TextOf (Text));
  if (Trimmed.Len && !TextEqStr (Trimmed, "NO NAME")) {
    Out = TextDup (Trimmed);
  }
  FreePool (Text);
  return Out;
}

// Device paths

// Returns the size of the device path, all instances and the end-of-entire
// node included, or 0 for a node shorter than its header.
STATIC UINTN
PathSize (IN CONST UINT8 *Path)
{
  UINTN Size = 0;

  for (;;) {
    CONST UINT8 *Node = Path + Size;
    UINTN        Len  = ReadLe16 (Node + 2);

    if (Len < 4) {
      return 0;
    }
    Size += Len;
    if (Node[0] == UEFI_TYPE_END && Node[1] == UEFI_END_ENTIRE) {
      return Size;
    }
  }
}

BOOLEAN
PathsHandlePath (
  IN EFI_HANDLE    Handle,
  OUT OABL_BUFFER *Out
)
{
  CONST UINT8 *DevicePath = GetProtocol (Handle, &gEfiDevicePathProtocolGuid);
  UINTN        Size;

  *Out = (OABL_BUFFER){0};
  if (!DevicePath || !(Size = PathSize (DevicePath))) {
    return FALSE;
  }
  return BufferFrom (Out, DevicePath, Size);
}

// Checks that Path holds a complete device path within Size bytes. Bytes
// after the end-of-entire node are allowed.
STATIC BOOLEAN
ValidPath (
  IN CONST UINT8 *Path,
  IN UINTN        Size
)
{
  UINTN At = 0;

  for (;;) {
    UINTN Len;

    if (Size - At < 4) {
      return FALSE;
    }
    Len = ReadLe16 (Path + At + 2);
    if (Len < 4 || Len > Size - At) {
      return FALSE;
    }
    if (Path[At] == UEFI_TYPE_END && Path[At + 1] == UEFI_END_ENTIRE) {
      return TRUE;
    }
    At += Len;
  }
}

CHAR8 *
PathsDescribe (
  IN CONST UINT8 *Path,
  IN UINTN        Size
)
{
  EFI_DEVICE_PATH_TO_TEXT_PROTOCOL *ToText = NULL;
  EFI_HANDLE                       *Handles;
  UINTN                             Count;
  CHAR16                           *Text;
  CHAR8                            *Ascii;

  if (!ValidPath (Path, Size)) {
    return AsciiStrDup ("?");
  }
  Count = LocateHandles (&gEfiDevicePathToTextProtocolGuid, &Handles);
  if (Count) {
    ToText = GetProtocol (Handles[0], &gEfiDevicePathToTextProtocolGuid);
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  if (!ToText) {
    return AsciiStrDup ("?");
  }
  Text = ToText->ConvertDevicePathToText (
    (CONST EFI_DEVICE_PATH_PROTOCOL *)Path, TRUE, TRUE
  );
  if (!Text) {
    return AsciiStrDup ("?");
  }
  Ascii = Utf16ToStr ((CONST UINT16 *)Text);
  gBS->FreePool (Text);
  return Ascii;
}

// Volumes

BOOT_SOURCE_MEDIUM
VolumesMedium (IN EFI_HANDLE Handle)
{
  EFI_BLOCK_IO_PROTOCOL *BlockIo =
    GetProtocol (Handle, &gEfiBlockIoProtocolGuid);
  BOOLEAN            Removable = BlockIo && BlockIo->Media->RemovableMedia;
  BOOT_SOURCE_MEDIUM Medium    = BootSourceMediumOf (Removable);
  OABL_BUFFER        Path      = {0};
  UEFI_NODE          Node;
  UINTN              Offset = 0;

  PathsHandlePath (Handle, &Path);
  while (UefiNextNode (Path.Data, Path.Len, &Offset, &Node)) {
    BootSourceMediumNode (&Medium, Node.Type, Node.SubType);
  }
  BufferFree (&Path);
  return Medium;
}

VOID
VolumesFileSystems (IN OUT VOLUME_FS_LIST *Out)
{
  EFI_HANDLE *Handles;
  UINTN Count = LocateHandles (&gEfiSimpleFileSystemProtocolGuid, &Handles);

  for (UINTN Index = 0; Index < Count; Index++) {
    if (!VecPush (
          Out, (VOLUME_FS){Handles[Index], VolumesMedium (Handles[Index])}
        )) {
      break;
    }
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
}

VOID
VolumesAllowed (
  IN BOOT_SOURCE              Source,
  IN BOOLEAN                  CfwOnInternal,
  IN BOOLEAN                  AltVolumeBoot,
  IN OUT VOLUMES_HANDLE_LIST *Out
)
{
  VOLUME_FS_LIST      FileSystems = {0};
  BOOT_SOURCE_MEDIUM *Media;
  UINTN              *Order;
  UINTN               Count;

  VolumesFileSystems (&FileSystems);
  Media = AllocateZeroArray (
    FileSystems.Count ? FileSystems.Count : 1, sizeof (*Media)
  );
  Order = AllocateZeroArray (
    FileSystems.Count ? FileSystems.Count : 1, sizeof (*Order)
  );
  if (Media && Order) {
    for (UINTN Index = 0; Index < FileSystems.Count; Index++) {
      Media[Index] = FileSystems.Items[Index].Medium;
    }
    Count = BootSourceSelect (
      Source, CfwOnInternal, AltVolumeBoot, Media, FileSystems.Count, Order
    );
    if (VecReserve (Out, Count)) {
      for (UINTN Index = 0; Index < Count; Index++) {
        Out->Items[Out->Count++] = FileSystems.Items[Order[Index]].Handle;
      }
    }
  }
  FreePool (Media);
  FreePool (Order);
  VecFree (&FileSystems);
}

// Raw FAT32 reads

// Page alignment satisfies any IoAlign. The size must be a multiple of the
// block size.
#define BOUNCE_SIZE  ((UINTN)64 << 10)
#define BOUNCE_PAGES (BOUNCE_SIZE / 4096)

typedef struct _READER {
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  UINT32                 MediaId;
  UINTN                  BlockSize;
  UINT8                 *Bounce;
} READER;

// FAT32_READ callback. Reads whole blocks into the bounce buffer, at most
// BOUNCE_SIZE at a time, and copies out the bytes asked for.
STATIC BOOLEAN
ReadAt (
  IN UINT64  Offset,
  OUT UINT8 *Buffer,
  IN UINTN   Size,
  IN VOID   *Context
)
{
  READER *Reader = Context;

  while (Size > 0) {
    UINT64 Lba    = Offset / Reader->BlockSize;
    UINTN  Skip   = (UINTN)(Offset % Reader->BlockSize);
    UINTN  Need   = Skip + Size;
    UINTN  Blocks = Need / Reader->BlockSize + (Need % Reader->BlockSize != 0);
    UINTN  Bytes  = Blocks > BOUNCE_SIZE / Reader->BlockSize
                      ? BOUNCE_SIZE
                      : Blocks * Reader->BlockSize;
    UINTN  Take   = Bytes - Skip < Size ? Bytes - Skip : Size;

    if (Reader->BlockIo->ReadBlocks (
          Reader->BlockIo, Reader->MediaId, Lba, Bytes, Reader->Bounce
        ) != EFI_SUCCESS) {
      return FALSE;
    }
    CopyMem (Buffer, Reader->Bounce + Skip, Take);
    Offset += Take;
    Buffer += Take;
    Size -= Take;
  }
  return TRUE;
}

BOOLEAN
RawFatReadRange (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Path,
  IN UINT64        Offset,
  IN UINTN         Length,
  OUT OABL_BUFFER *Out
)
{
  // Not opened exclusively, so the partition stays bound to its drivers.
  EFI_BLOCK_IO_PROTOCOL *BlockIo = GetProtocol (Fs, &gEfiBlockIoProtocolGuid);
  EFI_PHYSICAL_ADDRESS   Pages;
  FAT32_VOLUME           V;
  FAT32_FILE             File;
  READER                 Reader;
  BOOLEAN                Ok = FALSE;
  UINTN                  BlockSize;

  if (!BlockIo) {
    return FALSE;
  }
  BlockSize = BlockIo->Media->BlockSize;
  if (!BlockIo->Media->MediaPresent || BlockSize == 0 ||
      BlockSize > BOUNCE_SIZE || BOUNCE_SIZE % BlockSize != 0) {
    return FALSE;
  }
  Reader = (READER){
    .BlockIo   = BlockIo,
    .MediaId   = BlockIo->Media->MediaId,
    .BlockSize = BlockSize,
  };
  if (gBS->AllocatePages (
        AllocateAnyPages, EfiLoaderData, BOUNCE_PAGES, &Pages
      ) != EFI_SUCCESS) {
    return FALSE;
  }
  Reader.Bounce = (UINT8 *)(UINTN)Pages;
  if (Fat32Mount (&V, (UINT32)BlockSize, ReadAt, &Reader)) {
    Ok =
      Fat32Find (&V, Path, &File) && Fat32Read (&V, &File, Offset, Length, Out);
    Fat32Free (&V);
  }
  gBS->FreePages (Pages, BOUNCE_PAGES);
  return Ok;
}
