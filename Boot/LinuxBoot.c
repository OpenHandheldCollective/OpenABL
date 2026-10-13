/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  LinuxBoot - starts EFI images the way a Boot Loader Specification loader
  does.
*/

#include "Boot/LinuxBoot.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Boot/BootUtil.h"
#include "Core/EfiData.h"
#include "Core/Pe.h"
#include "Efi/Abl.h"
#include "Efi/Files.h"
#include "Efi/Efi.h"
#include "Efi/Serial.h"

// LINUX_EFI_INITRD_MEDIA_GUID.
STATIC EFI_GUID mInitrdMedia = {
  0x5568e427,
  0x68fc,
  0x4f3d,
  {0xac, 0x74, 0xca, 0x55, 0x52, 0x31, 0xcc, 0x68}};

// Largest kernel or initrd file read, and largest inflated kernel.
#define FILE_LIMIT ((UINTN)512 << 20)

// Gzip header flags (RFC 1952).
#define GZIP_FHCRC    0x02
#define GZIP_FEXTRA   0x04
#define GZIP_FNAME    0x08
#define GZIP_FCOMMENT 0x10

// Longest file name Qualcomm's decompress can skip. It gives up on the
// 256th character.
#define DECOMPRESS_NAME_LIMIT 255

// Gets the offset in *At of a gzip member's deflate data, past its header.
STATIC BOOLEAN
GzipPayload (
  IN CONST UINT8 *Data,
  IN UINTN        Size,
  OUT UINTN      *At
)
{
  STATIC CONST UINT8 Strings[] = {GZIP_FNAME, GZIP_FCOMMENT};
  UINT8              Flags;
  UINTN              Offset = 10;

  if (Size < 18 || Data[0] != 0x1f || Data[1] != 0x8b || Data[2] != 8) {
    return FALSE;
  }
  Flags = Data[3];
  if (Flags & GZIP_FEXTRA) {
    if (Offset + 1 >= Size) {
      return FALSE;
    }
    Offset += 2 + (UINTN)ReadLe16 (Data + Offset);
  }
  for (UINTN Index = 0; Index < sizeof (Strings); Index++) {
    if (!(Flags & Strings[Index])) {
      continue;
    }
    if (Offset > Size) {
      return FALSE;
    }
    while (Offset < Size && Data[Offset]) {
      Offset++;
    }
    if (Offset == Size) {
      return FALSE;
    }
    Offset++;
  }
  if (Flags & GZIP_FHCRC) {
    Offset += 2;
  }
  if (Offset > Size) {
    return FALSE;
  }
  *At = Offset;
  return TRUE;
}

// Inflates the gzip member in File with the ABL's decompress. It only skips
// a plain header and a short file name, so any other header is replaced by
// a flagless one ahead of the deflate data at At. Its output buffer must be
// larger than both the input and the output, so it is sized from ISIZE in
// the file's last four bytes, plus slack. Data after the member is ignored,
// but if it makes those bytes too small the inflate fails.
STATIC EFI_STATUS
InflateGzip (
  IN CONST OABL_BUFFER *File,
  IN UINTN              At,
  OUT OABL_BUFFER      *Out
)
{
  CONST UINT8 *Data = File->Data, *In = File->Data;
  UINTN        Size    = File->Len;
  UINTN        InSize  = File->Len;
  UINTN        Written = 0;
  UINTN        Used    = 0;
  UINTN        OutSize;
  UINT32       Isize = ReadLe32 (Data + Size - 4);
  UINT8        Flags = Data[3];
  UINT8       *Copy  = NULL;
  BOOLEAN      Plain = !(Flags & (GZIP_FHCRC | GZIP_FEXTRA | GZIP_FCOMMENT));
  EFI_STATUS   Status;

  *Out = (OABL_BUFFER){0};
  if (Isize == 0 || Isize > FILE_LIMIT) {
    return EFI_BAD_BUFFER_SIZE;
  }
  if (Plain && (Flags & GZIP_FNAME)) {
    UINTN Len = 0;

    while (10 + Len < Size && Data[10 + Len]) {
      Len++;
    }
    Plain = Len <= DECOMPRESS_NAME_LIMIT;
  }
  if (!Plain) {
    InSize = 10 + (Size - At);
    Copy   = AllocatePool (InSize);
    if (!Copy) {
      return EFI_OUT_OF_RESOURCES;
    }
    CopyMem (Copy, Data, 10);
    Copy[3] = 0;
    CopyMem (Copy + 10, Data + At, Size - At);
    In = Copy;
  }
  OutSize   = (Isize > InSize ? Isize : InSize) + 4096;
  Out->Data = AllocatePool (OutSize);
  if (!Out->Data) {
    FreePool (Copy);
    return EFI_OUT_OF_RESOURCES;
  }
  Out->Cap = OutSize;
  Status   = AblLibInflate (In, InSize, Out->Data, OutSize, &Written, &Used);
  FreePool (Copy);
  if (Status != EFI_SUCCESS) {
    BufferFree (Out);
    return Status;
  }
  Out->Len = Written;
  return EFI_SUCCESS;
}

// Puts a loadable PE image for the kernel in Out. Takes over File if it is
// one already, otherwise inflates it.
STATIC BOOLEAN
KernelImage (
  IN OUT OABL_BUFFER *File,
  OUT OABL_BUFFER    *Out,
  OUT CHAR8         **Err
)
{
  CONST UINT8 Zstd[4] = {0x28, 0xb5, 0x2f, 0xfd},
              Lz4[4]  = {0x02, 0x21, 0x4c, 0x18};
  UINTN       At;

  *Out = (OABL_BUFFER){0};
  if (PeIsPe (File->Data, File->Len)) {
    *Out  = *File;
    *File = (OABL_BUFFER){0};
    return TRUE;
  }
  if (GzipPayload (File->Data, File->Len, &At)) {
    OABL_BUFFER Image;
    EFI_STATUS  Status = InflateGzip (File, At, &Image);

    if (Status != EFI_SUCCESS) {
      *Err = SPrintAlloc (
        "inflating the gzip kernel failed: %s",
        StatusText (Status, (CHAR8[STATUS_TEXT_SIZE]){0})
      );
      return FALSE;
    }
    SerialLog (
      "  inflated gzip kernel: %zu -> %zu bytes", File->Len, Image.Len
    );
    if (PeIsPe (Image.Data, Image.Len)) {
      *Out = Image;
      return TRUE;
    }
    BufferFree (&Image);
    *Err =
      AsciiStrDup ("the inflated kernel isn't an EFI image (no EFI stub?)");
    return FALSE;
  }
  if (File->Len >= 4 && CompareMem (File->Data, Zstd, 4) == 0) {
    *Err = AsciiStrDup ("zstd-compressed kernels aren't supported");
  } else if (File->Len >= 4 && CompareMem (File->Data, Lz4, 4) == 0) {
    *Err = AsciiStrDup ("LZ4-compressed kernels aren't supported");
  } else {
    *Err = AsciiStrDup ("not an EFI image (a kernel needs CONFIG_EFI_STUB)");
  }
  return FALSE;
}

BOOLEAN
LinuxBootLoad (
  IN EFI_HANDLE   Fs,
  IN OABL_BYTES   Dev,
  IN CONST CHAR8 *Path,
  OUT EFI_HANDLE *Image,
  OUT CHAR8     **Err
)
{
  OABL_BUFFER File;
  OABL_BUFFER Loadable;
  OABL_BUFFER Node   = {0};
  OABL_BUFFER Full   = {0};
  EFI_HANDLE  Handle = NULL;
  EFI_STATUS  Status;
  BOOLEAN     Ok;

  *Image = NULL;
  *Err   = NULL;
  if (!FilesRead (Fs, Path, FILE_LIMIT, &File)) {
    *Err = SPrintAlloc ("can't read %s", Path);
    return FALSE;
  }
  Ok = KernelImage (&File, &Loadable, Err);
  BufferFree (&File);
  if (!Ok) {
    return FALSE;
  }
  if (!UefiFilePathNode (Path, &Node) ||
      !UefiJoin ((OABL_BYTES[]){Dev, BufferBytes (&Node)}, 2, &Full) ||
      !DevicePathValid (Full.Data, Full.Len)) {
    *Err = AsciiStrDup ("bad device path");
    Ok   = FALSE;
  } else {
    Status = gBS->LoadImage (
      FALSE, gImageHandle, (EFI_DEVICE_PATH_PROTOCOL *)Full.Data, Loadable.Data,
      Loadable.Len, &Handle
    );
    if (Status != EFI_SUCCESS) {
      *Err = SPrintAlloc (
        "LoadImage %s: %s", Path,
        StatusDebug (Status, (CHAR8[BOOT_STATUS_DEBUG_SIZE]){0})
      );
      Ok = FALSE;
    }
  }
  BufferFree (&Full);
  BufferFree (&Node);
  BufferFree (&Loadable);
  if (Ok) {
    *Image = Handle;
  }
  return Ok;
}

BOOLEAN
LinuxBootSetOptions (
  IN EFI_HANDLE         Image,
  IN CONST OABL_BUFFER *Options,
  OUT CHAR8           **Err
)
{
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
  EFI_STATUS                 Status;

  *Err = NULL;
  if (!Options->Len) {
    return TRUE;
  }
  Status =
    OpenExclusive (Image, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  if (Status != EFI_SUCCESS || !LoadedImage) {
    *Err =
      AsciiStrDup (StatusDebug (Status, (CHAR8[BOOT_STATUS_DEBUG_SIZE]){0}));
    return FALSE;
  }
  LoadedImage->LoadOptions     = Options->Data;
  LoadedImage->LoadOptionsSize = (UINT32)Options->Len;
  CloseExclusive (Image, &gEfiLoadedImageProtocolGuid);
  return TRUE;
}

BOOLEAN
LinuxBootTextOptions (
  IN CONST CHAR8  *Text,
  OUT OABL_BUFFER *Out
)
{
  UINTN   Units = 0;
  UINT16 *Wide;
  BOOLEAN Ok;

  *Out = (OABL_BUFFER){0};
  if (!*Text) {
    return TRUE;
  }
  Wide = Utf16FromText (TextOf (Text), &Units);
  Ok   = Wide && BufferReserve (Out, (Units + 1) * 2);
  for (UINTN Index = 0; Ok && Index <= Units; Index++) {
    BufferAppendLe16 (Out, Wide[Index]);
  }
  FreePool (Wide);
  return Ok;
}

// Proto must come first, so LoadFile2's This points to the whole loader.
typedef struct _INITRD_LOADER {
  EFI_LOAD_FILE2_PROTOCOL Proto;
  CONST UINT8            *Data;
  UINTN                   Len;
} INITRD_LOADER;

typedef struct _LINUX_BOOT_INITRD {
  EFI_HANDLE    Handle;
  INITRD_LOADER Loader;
  // VenMedia(LINUX_EFI_INITRD_MEDIA_GUID)/End.
  UINT8       Path[24];
  OABL_BUFFER Data;
} LINUX_BOOT_INITRD;

STATIC EFI_STATUS EFIAPI
InitrdLoadFile (
  IN EFI_LOAD_FILE2_PROTOCOL  *This,
  IN EFI_DEVICE_PATH_PROTOCOL *FilePath,
  IN BOOLEAN                   BootPolicy,
  IN OUT UINTN                *BufferSize,
  IN VOID                     *Buffer OPTIONAL
)
{
  CONST INITRD_LOADER *Loader;

  (VOID)FilePath;
  if (!This || !BufferSize) {
    return EFI_INVALID_PARAMETER;
  }
  if (BootPolicy) {
    return EFI_UNSUPPORTED;
  }
  // This is the first field of one of our INITRD_LOADERs.
  Loader = (CONST INITRD_LOADER *)This;
  if (!Buffer || *BufferSize < Loader->Len) {
    *BufferSize = Loader->Len;
    return EFI_BUFFER_TOO_SMALL;
  }
  CopyMem (Buffer, Loader->Data, Loader->Len);
  *BufferSize = Loader->Len;
  return EFI_SUCCESS;
}

BOOLEAN
LinuxBootInstallInitrd (
  IN EFI_HANDLE                Fs,
  IN CONST BOOT_ENTRY_STRINGS *Paths,
  OUT LINUX_BOOT_INITRD      **Out,
  OUT CHAR8                  **Err
)
{
  LINUX_BOOT_INITRD        *Initrd;
  OABL_BUFFER               Data = {0};
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;
  EFI_HANDLE                Handle = NULL;
  EFI_STATUS                Status;

  *Out = NULL;
  *Err = NULL;
  if (!Paths->Count) {
    return TRUE;
  }
  for (UINTN Index = 0; Index < Paths->Count; Index++) {
    OABL_BUFFER File;
    BOOLEAN     Ok;

    if (!FilesRead (Fs, Paths->Items[Index], FILE_LIMIT, &File)) {
      *Err = SPrintAlloc ("can't read initrd %s", Paths->Items[Index]);
      BufferFree (&Data);
      return FALSE;
    }
    Ok = BufferAppend (&Data, File.Data, File.Len) &&
         BufferAppendZeros (&Data, AlignUp (Data.Len, 4) - Data.Len);
    BufferFree (&File);
    if (!Ok) {
      *Err = SPrintAlloc ("can't read initrd %s", Paths->Items[Index]);
      BufferFree (&Data);
      return FALSE;
    }
  }
  Initrd = AllocateZeroPool (sizeof (*Initrd));
  if (!Initrd) {
    *Err = AsciiStrDup ("out of memory for the initrd");
    BufferFree (&Data);
    return FALSE;
  }
  Initrd->Path[0] = UEFI_TYPE_MEDIA;
  Initrd->Path[1] = 0x03;
  Initrd->Path[2] = 20;
  Initrd->Path[3] = 0;
  CopyMem (Initrd->Path + 4, &mInitrdMedia, 16);
  CopyMem (Initrd->Path + 20, gUefiEndNode, 4);
  Initrd->Data = Data;
  DevicePath   = (EFI_DEVICE_PATH_PROTOCOL *)Initrd->Path;
  if (gBS->LocateDevicePath (
        &gEfiLoadFile2ProtocolGuid, &DevicePath, &Handle
      ) == EFI_SUCCESS) {
    *Err = AsciiStrDup ("another initrd is already registered");
    goto Fail;
  }
  Initrd->Loader = (INITRD_LOADER){
    .Proto = {.LoadFile = InitrdLoadFile},
    .Data  = Initrd->Data.Data,
    .Len   = Initrd->Data.Len,
  };
  Handle = NULL;
  Status = gBS->InstallProtocolInterface (
    &Handle, &gEfiDevicePathProtocolGuid, EFI_NATIVE_INTERFACE, Initrd->Path
  );
  if (Status != EFI_SUCCESS) {
    *Err = SPrintAlloc (
      "installing the initrd path: %s",
      StatusDebug (Status, (CHAR8[BOOT_STATUS_DEBUG_SIZE]){0})
    );
    goto Fail;
  }
  Status = gBS->InstallProtocolInterface (
    &Handle, &gEfiLoadFile2ProtocolGuid, EFI_NATIVE_INTERFACE,
    &Initrd->Loader.Proto
  );
  if (Status != EFI_SUCCESS) {
    gBS->UninstallProtocolInterface (
      Handle, &gEfiDevicePathProtocolGuid, Initrd->Path
    );
    *Err = SPrintAlloc (
      "installing LoadFile2: %s",
      StatusDebug (Status, (CHAR8[BOOT_STATUS_DEBUG_SIZE]){0})
    );
    goto Fail;
  }
  Initrd->Handle = Handle;
  SerialLog (
    "  initrd: %zu file(s), %zu bytes, served through LoadFile2", Paths->Count,
    Initrd->Data.Len
  );
  *Out = Initrd;
  return TRUE;
Fail:
  BufferFree (&Initrd->Data);
  FreePool (Initrd);
  return FALSE;
}

VOID
LinuxBootInitrdFree (IN LINUX_BOOT_INITRD *Initrd OPTIONAL)
{
  if (!Initrd) {
    return;
  }
  gBS->UninstallProtocolInterface (
    Initrd->Handle, &gEfiLoadFile2ProtocolGuid, &Initrd->Loader.Proto
  );
  gBS->UninstallProtocolInterface (
    Initrd->Handle, &gEfiDevicePathProtocolGuid, Initrd->Path
  );
  BufferFree (&Initrd->Data);
  FreePool (Initrd);
}
