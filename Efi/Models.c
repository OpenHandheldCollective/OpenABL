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
// Limit on the size of a .dtb file.
#define DTB_MAX ((UINTN)4 << 20)

VOID
ModelsFree (IN OUT SCANNED_MODELS *Models)
{
  for (UINTN Index = 0; Index < Models->Count; Index++) {
    FreePool (Models->Items[Index].Name);
  }
  VecFree (Models);
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
  DT_BLOBS Dtbs;
  UINTN    Added = 0;

  DtFindAll (Region, Size, &Dtbs);
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
    if (!Known) {
      DT_GAMEPAD Gamepad;
      BOOLEAN    HasGamepad =
        DtGamepadConfig (Blob, Dtbs.Items[Index].Size, &Gamepad);

      if (VecPush (
            Found, (SCANNED_MODEL){Info.Model, Info.HasPanelRotation,
                                   Info.PanelRotation, HasGamepad, Gamepad}
          )) {
        // Found owns the name now.
        Info.Model = NULL;
        Added++;
      }
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

BOOLEAN
ModelsDevicetreeFor (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Model,
  OUT OABL_BUFFER *Out
)
{
  *Out = (OABL_BUFFER){0};
  for (UINTN Dir = 0; Dir < N_DTB_DIRS; Dir++) {
    FIND_VISIT Find = {Model, Out};

    if (ReadDtbs (Fs, mDtbDirs[Dir], FindVisit, &Find)) {
      return TRUE;
    }
  }
  return FALSE;
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
    for (UINTN Dir = 0; Dir < N_DTB_DIRS; Dir++) {
      SCAN_VISIT Scan  = {Out, 0};
      UINTN      First = Out->Count;

      ReadDtbs (Drives[Drive], mDtbDirs[Dir], ScanVisit, &Scan);
      if (Scan.Trees) {
        LogPrint (
          Log, "scan: drive %llu: %llu new model(s) from %s", (UINT64)Drive,
          (UINT64)(Out->Count - First), mDtbDirs[Dir]
        );
      }
    }
  }
  SortBy (Out->Items, Out->Count, sizeof (*Out->Items), ByName, NULL);
}
