/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Models - the device models the boot drives offer.
*/

#include "Efi/Models.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Core/AndroidImage.h"
#include "Core/DeviceTree.h"
#include "Base/Buffer.h"
#include "Efi/Files.h"

// The folders searched for devicetree files, in order, and only these.
// \boot\grub is where GRUB-based images keep them; \dtbs is used by
// dtbloader and distributions, \dtb by U-Boot, each also with the kernel's
// qcom subfolder.
STATIC CONST CHAR8 *CONST mDtbDirs[] = {
  "\\boot\\grub", "\\dtbs", "\\dtbs\\qcom", "\\dtb", "\\dtb\\qcom",
};

#define N_DTB_DIRS (sizeof (mDtbDirs) / sizeof (*mDtbDirs))

// Limit on .dtb files read per folder.
#define DTB_FILES_MAX 256

CONST CHAR8 *CONST gModelsBootImages[4] = {"\\KERNEL", "\\boot\\Image",
                                           "\\kernel.img", "\\boot.img"};

// Read first to tell whether a file is a boot image.
#define HEAD_SIZE 4096
// Limit for a file that isn't a boot image; the ABL searches it whole.
#define WHOLE_MAX ((UINTN)128 << 20)
#define DTB_MAX   ((UINTN)4 << 20)

#define N_BOOT_IMAGES (sizeof (gModelsBootImages) / sizeof (*gModelsBootImages))

BOOLEAN
ModelsIsBootImage (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path
)
{
  OABL_BUFFER Head = {0};
  BOOLEAN     Ok;

  Ok = FilesReadPart (Fs, Path, 0, sizeof (ANDROID_BOOT_MAGIC) - 1, &Head) &&
       Head.Len >= sizeof (ANDROID_BOOT_MAGIC) - 1 &&
       CompareMem (
         Head.Data, ANDROID_BOOT_MAGIC, sizeof (ANDROID_BOOT_MAGIC) - 1
       ) == 0;
  BufferFree (&Head);
  return Ok;
}

VOID
ModelsFree (IN OUT SCANNED_MODELS *Models)
{
  for (UINTN Index = 0; Index < Models->Count; Index++) {
    FreePool (Models->Items[Index].Name);
  }
  VecFree (Models);
}

// Finds the first devicetree blob in Region whose model is Model.
STATIC BOOLEAN
BlobFor (
  IN CONST UINT8  *Region,
  IN UINTN         Size,
  IN CONST CHAR8  *Model,
  OUT ANDROID_DTB *Out
)
{
  ANDROID_DTBS Dtbs;
  BOOLEAN      Found = FALSE;

  AndroidFindDtbs (Region, Size, &Dtbs);
  for (UINTN Index = 0; Index < Dtbs.Count && !Found; Index++) {
    CONST UINT8 *Blob = Region + Dtbs.Items[Index].Offset;
    DT_INFO      Info;

    if (!DtInspect (Blob, Dtbs.Items[Index].Size, &Info)) {
      continue;
    }
    if (AsciiStrEq (Info.Model, Model)) {
      *Out  = Dtbs.Items[Index];
      Found = TRUE;
    }
    DtInfoFree (&Info);
  }
  VecFree (&Dtbs);
  return Found;
}

// Adds Region's models that aren't in Found yet. Returns how many; *Trees
// counts the devicetrees read.
STATIC UINTN
AddModels (
  IN OUT SCANNED_MODELS *Found,
  IN CONST UINT8        *Region,
  IN UINTN               Size,
  IN OUT UINTN          *Trees
)
{
  ANDROID_DTBS Dtbs;
  UINTN        Added = 0;

  AndroidFindDtbs (Region, Size, &Dtbs);
  for (UINTN Index = 0; Index < Dtbs.Count; Index++) {
    CONST UINT8 *Blob = Region + Dtbs.Items[Index].Offset;
    DT_INFO      Info;
    BOOLEAN      Known = FALSE;

    if (!DtInspect (Blob, Dtbs.Items[Index].Size, &Info)) {
      continue;
    }
    (*Trees)++;
    for (UINTN Index2 = 0; Index2 < Found->Count && !Known; Index2++) {
      Known = AsciiStrEq (Found->Items[Index2].Name, Info.Model);
    }
    if (!Known && VecPush (
                    Found, (SCANNED_MODEL){Info.Model, Info.HasPanelRotation,
                                           Info.PanelRotation}
                  )) {
      // Found owns the name now.
      Info.Model = NULL;
      Added++;
    }
    DtInfoFree (&Info);
  }
  VecFree (&Dtbs);
  return Added;
}

// Called for each .dtb file ReadDtbs reads. Returning TRUE stops it.
typedef BOOLEAN (*DTB_VISIT) (
  IN OUT VOID        *Context,
  IN OUT OABL_BUFFER *Blob
);

// Reads the .dtb files in Dir, not its subfolders, and calls Visit with
// each, up to DTB_FILES_MAX of them. TRUE if Visit stopped it.
STATIC BOOLEAN
ReadDtbs (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Dir,
  IN DTB_VISIT    Visit,
  IN OUT VOID    *Context
)
{
  FILES_DIR_ENTRIES Names = {0};
  BOOLEAN           Stop  = FALSE;
  UINTN             Read  = 0;

  FilesList (Fs, Dir, &Names);
  for (UINTN Index = 0; Index < Names.Count && !Stop && Read < DTB_FILES_MAX;
       Index++) {
    CONST FILES_DIR_ENTRY *Entry = &Names.Items[Index];
    OABL_TEXT              Name  = TextOf (Entry->Name);
    OABL_BUFFER            Blob  = {0};
    CHAR8                 *Path;

    if (Entry->IsDir || Name.Len < 4 ||
        !TextEqNoCase (TextSub (Name, Name.Len - 4, Name.Len), ".dtb") ||
        !(Path = SPrintAlloc ("%s\\%s", Dir, Entry->Name))) {
      continue;
    }
    Read++;
    if (FilesRead (Fs, Path, DTB_MAX, &Blob)) {
      Stop = Visit (Context, &Blob);
    }
    BufferFree (&Blob);
    FreePool (Path);
  }
  FilesEntriesFree (&Names);
  return Stop;
}

// State for ModelsScan's walk of one folder.
typedef struct _SCAN_VISIT {
  SCANNED_MODELS *Found;
  UINTN           Trees;
} SCAN_VISIT;

STATIC BOOLEAN
ScanVisit (
  IN OUT VOID        *Context,
  IN OUT OABL_BUFFER *Blob
)
{
  SCAN_VISIT *Scan = Context;

  AddModels (Scan->Found, Blob->Data, Blob->Len, &Scan->Trees);
  return FALSE;
}

// State for ModelsDevicetreeFor's walk.
typedef struct _FIND_VISIT {
  CONST CHAR8 *Model;
  OABL_BUFFER *Out;
} FIND_VISIT;

STATIC BOOLEAN
FindVisit (
  IN OUT VOID        *Context,
  IN OUT OABL_BUFFER *Blob
)
{
  FIND_VISIT *Find  = Context;
  BOOLEAN     Found = FALSE;
  DT_INFO     Info;

  if (DtInspect (Blob->Data, Blob->Len, &Info)) {
    Found = AsciiStrEq (Info.Model, Find->Model);
    DtInfoFree (&Info);
  }
  if (Found) {
    // Out owns the buffer now.
    *Find->Out = *Blob;
    *Blob      = (OABL_BUFFER){0};
  }
  return Found;
}

// With Raw, reads straight off FAT32 instead of through the file system.
STATIC BOOLEAN
ReadRange (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Path,
  IN UINT64        Offset,
  IN UINTN         Length,
  IN BOOLEAN       Raw,
  OUT OABL_BUFFER *Out
)
{
  if (Raw) {
    return RawFatReadRange (Fs, Path, Offset, Length, Out);
  }
  return FilesReadPart (Fs, Path, Offset, Length, Out);
}

// Reads the part of the boot image at Path that holds its devicetrees: the
// DTB section, or the last 4 MiB of the kernel. If the appended
// devicetrees may start before that window, the whole kernel is read. Head
// holds the image's first bytes.
STATIC BOOLEAN
BootImage (
  IN EFI_HANDLE         Fs,
  IN CONST CHAR8       *Path,
  IN CONST OABL_BUFFER *Head,
  IN BOOLEAN            Raw,
  OUT OABL_BUFFER      *Out
)
{
  ANDROID_BOOT_IMAGE Image;
  UINT64             At;
  UINT64             Len;
  BOOLEAN            Ok = FALSE;

  if (!AndroidBootImageParse (Head->Data, Head->Len, &Image)) {
    return FALSE;
  }
  if (AndroidDtbWindow (&Image, &At, &Len) &&
      ReadRange (Fs, Path, At, (UINTN)Len, Raw, Out)) {
    Ok = TRUE;
    if (AndroidWindowTooShort (&Image, At, Len, Out->Data, Out->Len)) {
      BufferFree (Out);
      Ok =
        ReadRange (Fs, Path, Image.Kernel.Offset, Image.Kernel.Size, Raw, Out);
    }
  }
  AndroidBootImageFree (&Image);
  if (!Ok) {
    BufferFree (Out);
  }
  return Ok;
}

// Reads the devicetree region of the drive's first boot image file, or the
// whole file if it isn't a boot image.
STATIC BOOLEAN
BootImageRegion (
  IN EFI_HANDLE    Fs,
  OUT OABL_BUFFER *Out
)
{
  for (UINTN Index = 0; Index < N_BOOT_IMAGES; Index++) {
    CONST CHAR8       *Path = gModelsBootImages[Index];
    ANDROID_BOOT_IMAGE Image;
    OABL_BUFFER        Head = {0};
    BOOLEAN            Ok;

    if (!ReadRange (Fs, Path, 0, HEAD_SIZE, FALSE, &Head)) {
      BufferFree (&Head);
      continue;
    }
    if (AndroidBootImageParse (Head.Data, Head.Len, &Image)) {
      AndroidBootImageFree (&Image);
      Ok = BootImage (Fs, Path, &Head, FALSE, Out);
    } else {
      Ok = FilesRead (Fs, Path, WHOLE_MAX, Out);
      if (!Ok) {
        BufferFree (Out);
      }
    }
    BufferFree (&Head);
    return Ok;
  }
  // XBL's FAT driver failed on every file. Read straight off FAT32, boot
  // images only.
  for (UINTN Index = 0; Index < N_BOOT_IMAGES; Index++) {
    CONST CHAR8 *Path = gModelsBootImages[Index];
    OABL_BUFFER  Head = {0};
    BOOLEAN      Ok;

    if (!ReadRange (Fs, Path, 0, HEAD_SIZE, TRUE, &Head)) {
      BufferFree (&Head);
      continue;
    }
    Ok = BootImage (Fs, Path, &Head, TRUE, Out);
    BufferFree (&Head);
    if (Ok) {
      return TRUE;
    }
  }
  return FALSE;
}

// Adds the models in the drive's boot image. FALSE if it has none.
STATIC BOOLEAN
BootImageDtbs (
  IN EFI_HANDLE          Fs,
  IN OUT SCANNED_MODELS *Found,
  OUT UINTN             *Added
)
{
  OABL_BUFFER Region = {0};
  UINTN       Trees  = 0;

  if (!BootImageRegion (Fs, &Region)) {
    return FALSE;
  }
  *Added = AddModels (Found, Region.Data, Region.Len, &Trees);
  BufferFree (&Region);
  return TRUE;
}

BOOLEAN
ModelsDevicetreeFor (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Model,
  OUT OABL_BUFFER *Out
)
{
  OABL_BUFFER Blob = {0};
  ANDROID_DTB Dtb;
  BOOLEAN     Found;

  *Out = (OABL_BUFFER){0};
  for (UINTN Dir = 0; Dir < N_DTB_DIRS; Dir++) {
    FIND_VISIT Find = {Model, Out};

    if (ReadDtbs (Fs, mDtbDirs[Dir], FindVisit, &Find)) {
      return TRUE;
    }
  }
  if (!BootImageRegion (Fs, &Blob)) {
    return FALSE;
  }
  Found = BlobFor (Blob.Data, Blob.Len, Model, &Dtb) &&
          BufferFrom (Out, Blob.Data + Dtb.Offset, Dtb.Size);
  BufferFree (&Blob);
  return Found;
}

STATIC BOOLEAN
ByName (
  IN CONST VOID *A,
  IN CONST VOID *B,
  IN VOID       *Context
)
{
  CONST UINT8 *Left  = (CONST UINT8 *)((CONST SCANNED_MODEL *)A)->Name;
  CONST UINT8 *Right = (CONST UINT8 *)((CONST SCANNED_MODEL *)B)->Name;

  (VOID)Context;
  while (*Left && *Left == *Right) {
    Left++;
    Right++;
  }
  return *Left < *Right;
}

VOID
ModelsScan (
  IN CONST EFI_HANDLE   *Drives,
  IN UINTN               Count,
  IN LOG_SINK            Log,
  IN OUT SCANNED_MODELS *Out
)
{
  for (UINTN Drive = 0; Drive < Count; Drive++) {
    BOOLEAN FromFiles = FALSE;
    UINTN   Before    = Out->Count;
    UINTN   Added;

    for (UINTN Dir = 0; Dir < N_DTB_DIRS; Dir++) {
      SCAN_VISIT Scan  = {Out, 0};
      UINTN      First = Out->Count;

      ReadDtbs (Drives[Drive], mDtbDirs[Dir], ScanVisit, &Scan);
      if (Scan.Trees) {
        FromFiles = TRUE;
        LogPrint (
          Log, "scan: drive %llu: %llu new model(s) from %s", (UINT64)Drive,
          (UINT64)(Out->Count - First), mDtbDirs[Dir]
        );
      }
    }
    // The boot image is searched only if no devicetree files were found.
    if (!FromFiles && BootImageDtbs (Drives[Drive], Out, &Added)) {
      LogPrint (
        Log, "scan: drive %llu: %llu new model(s) from its boot image",
        (UINT64)Drive, (UINT64)(Out->Count - Before)
      );
    }
  }
  SortBy (Out->Items, Out->Count, sizeof (*Out->Items), ByName, NULL);
}
