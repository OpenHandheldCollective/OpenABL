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
#include "Core/Soc.h"
#include "Base/Buffer.h"
#include "Efi/Files.h"
#include "Efi/Quirks.h"

// The folders searched for devicetree files, in order, and only these.
// \boot\grub is where GRUB-based images keep them; \dtbs is used by
// dtbloader and distributions, \dtb by U-Boot, each also with the kernel's
// qcom subfolder.
STATIC CONST CHAR8 *CONST mDtbDirs[] = {
  "\\boot\\grub", "\\dtbs", "\\dtbs\\qcom", "\\dtb", "\\dtb\\qcom",
};

#define N_DTB_DIRS (sizeof (mDtbDirs) / sizeof (*mDtbDirs))

// Limit on the size of a .dtb file.
#define DTB_MAX ((UINTN)4 << 20)
// The first read of a .dtb file: its header and the root's properties.
#define DTB_HEAD ((UINTN)4 << 10)
// Limit on the strings block read with it.
#define DTB_STRINGS_MAX ((UINTN)64 << 10)

VOID
ModelsFree (IN OUT SCANNED_MODELS *Models)
{
  for (UINTN Index = 0; Index < Models->Count; Index++) {
    FreePool (Models->Items[Index].Name);
  }
  VecFree (Models);
}

// Bytes up to the first NUL, as DtInspect reads a model.
STATIC OABL_TEXT
UpToNul (IN OABL_BYTES Bytes)
{
  UINTN Len = 0;

  while (Len < Bytes.Len && Bytes.Ptr[Len]) {
    Len++;
  }
  return (OABL_TEXT){(CONST CHAR8 *)Bytes.Ptr, Len};
}

STATIC BOOLEAN
IsKnown (
  IN CONST SCANNED_MODELS *Found,
  IN OABL_TEXT             Model
)
{
  for (UINTN Index = 0; Index < Found->Count; Index++) {
    if (TextEqStr (Model, Found->Items[Index].Name)) {
      return TRUE;
    }
  }
  return FALSE;
}

// Adds Region's models for Soc (any SoC if NULL) that aren't in Found yet.
// Returns how many; *Trees counts the devicetrees read and *Others those
// for another SoC.
STATIC UINTN
AddModels (
  IN OUT SCANNED_MODELS *Found,
  IN CONST UINT8        *Region,
  IN UINTN               Size,
  IN CONST SOC_INFO     *Soc OPTIONAL,
  IN OUT UINTN          *Trees,
  IN OUT UINTN          *Others
)
{
  DT_BLOBS Dtbs;
  UINTN    Added = 0;

  DtFindAll (Region, Size, &Dtbs);
  for (UINTN Index = 0; Index < Dtbs.Count; Index++) {
    CONST UINT8 *Blob = Region + Dtbs.Items[Index].Offset;
    DT_INFO      Info;
    OABL_BYTES   List;

    if (!DtInspect (Blob, Dtbs.Items[Index].Size, &Info)) {
      continue;
    }
    (*Trees)++;
    if (Soc &&
        !(DtPropAt (Blob, Dtbs.Items[Index].Size, 0, "compatible", &List) &&
          SocMatches (Soc, List))) {
      (*Others)++;
    } else if (!IsKnown (Found, TextOf (Info.Model)) &&
               VecPush (
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

// A .dtb file's root model and compatible, read without the whole file.
typedef struct _DTB_PEEK {
  // FALSE if they couldn't be read this way, such as for a file with
  // several devicetrees; then the whole file has to be read.
  BOOLEAN    Known;
  BOOLEAN    HasModel;
  BOOLEAN    HasCompatible;
  OABL_BYTES Model;
  OABL_BYTES Compatible;
  // Model and Compatible point into Head.
  OABL_BUFFER Head;
  OABL_BUFFER Strings;
} DTB_PEEK;

// Reads the header and the root's properties at the start of Path, and its
// strings block at the end, rather than the whole file.
STATIC VOID
PeekDtb (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path,
  OUT DTB_PEEK   *Peek
)
{
  DT_LAYOUT Layout;
  DT_PEEK   Model;
  DT_PEEK   Compatible;
  UINTN     Length;

  *Peek = (DTB_PEEK){0};
  if (!FilesReadPart (Fs, Path, 0, DTB_HEAD, &Peek->Head) ||
      !DtLayoutOf (Peek->Head.Data, Peek->Head.Len, &Layout) ||
      (Length = Layout.Total - Layout.StringsOffset) > DTB_STRINGS_MAX) {
    return;
  }
  // One byte more: a file that goes on past the tree may hold more trees,
  // which only DtFindAll on the whole file finds.
  if (!FilesReadPart (
        Fs, Path, Layout.StringsOffset, Length + 1, &Peek->Strings
      ) ||
      Peek->Strings.Len != Length) {
    return;
  }
  Model = DtPeekRootProp (
    Peek->Head.Data, Peek->Head.Len, &Layout, Peek->Strings.Data,
    Peek->Strings.Len, "model", &Peek->Model
  );
  Compatible = DtPeekRootProp (
    Peek->Head.Data, Peek->Head.Len, &Layout, Peek->Strings.Data,
    Peek->Strings.Len, "compatible", &Peek->Compatible
  );
  if (Model == DT_PEEK_UNKNOWN || Compatible == DT_PEEK_UNKNOWN) {
    return;
  }
  Peek->Known         = TRUE;
  Peek->HasModel      = Model == DT_PEEK_FOUND;
  Peek->HasCompatible = Compatible == DT_PEEK_FOUND;
}

STATIC VOID
PeekFree (IN OUT DTB_PEEK *Peek)
{
  BufferFree (&Peek->Head);
  BufferFree (&Peek->Strings);
  *Peek = (DTB_PEEK){0};
}

// Called for each .dtb file before ReadDtbs reads all of it. Returning
// FALSE skips the file.
typedef BOOLEAN (*DTB_WANT) (
  IN OUT VOID       *Context,
  IN CONST DTB_PEEK *Peek
);

// Called for each .dtb file ReadDtbs reads. Returning TRUE stops it.
typedef BOOLEAN (*DTB_VISIT) (
  IN OUT VOID        *Context,
  IN OUT OABL_BUFFER *Blob
);

// Reads the .dtb files in Dir, not its subfolders, that Want wants and
// calls Visit with each. TRUE if Visit stopped it.
STATIC BOOLEAN
ReadDtbs (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Dir,
  IN DTB_WANT     Want,
  IN DTB_VISIT    Visit,
  IN OUT VOID    *Context
)
{
  FILES_DIR_ENTRIES Names = {0};
  BOOLEAN           Stop  = FALSE;

  FilesList (Fs, Dir, &Names);
  for (UINTN Index = 0; Index < Names.Count && !Stop; Index++) {
    CONST FILES_DIR_ENTRY *Entry = &Names.Items[Index];
    OABL_TEXT              Name  = TextOf (Entry->Name);
    OABL_BUFFER            Blob  = {0};
    DTB_PEEK               Peek;
    CHAR8                 *Path;

    if (Entry->IsDir || Name.Len < 4 ||
        !TextEqNoCase (TextSub (Name, Name.Len - 4, Name.Len), ".dtb") ||
        !(Path = SPrintAlloc ("%s\\%s", Dir, Entry->Name))) {
      continue;
    }
    PeekDtb (Fs, Path, &Peek);
    if (Want (Context, &Peek) && FilesRead (Fs, Path, DTB_MAX, &Blob)) {
      Stop = Visit (Context, &Blob);
    }
    PeekFree (&Peek);
    BufferFree (&Blob);
    FreePool (Path);
  }
  FilesEntriesFree (&Names);
  return Stop;
}

// State for ModelsScan's walk of one folder.
typedef struct _SCAN_VISIT {
  SCANNED_MODELS *Found;
  // The running SoC, or NULL to take every model.
  CONST SOC_INFO *Soc;
  UINTN           Trees;
  UINTN           Others;
} SCAN_VISIT;

// Reads the whole file only for a model of this SoC not found yet, which
// needs its panel rotation.
STATIC BOOLEAN
ScanWant (
  IN OUT VOID       *Context,
  IN CONST DTB_PEEK *Peek
)
{
  SCAN_VISIT *Scan = Context;

  if (!Peek->Known) {
    return TRUE;
  }
  // DtInspect needs a model.
  if (!Peek->HasModel) {
    return FALSE;
  }
  if (Scan->Soc &&
      !(Peek->HasCompatible && SocMatches (Scan->Soc, Peek->Compatible))) {
    Scan->Trees++;
    Scan->Others++;
    return FALSE;
  }
  if (IsKnown (Scan->Found, UpToNul (Peek->Model))) {
    Scan->Trees++;
    return FALSE;
  }
  return TRUE;
}

STATIC BOOLEAN
ScanVisit (
  IN OUT VOID        *Context,
  IN OUT OABL_BUFFER *Blob
)
{
  SCAN_VISIT *Scan = Context;

  AddModels (
    Scan->Found, Blob->Data, Blob->Len, Scan->Soc, &Scan->Trees, &Scan->Others
  );
  return FALSE;
}

// State for ModelsDevicetreeFor's walk.
typedef struct _FIND_VISIT {
  CONST CHAR8 *Model;
  OABL_BUFFER *Out;
} FIND_VISIT;

// Reads the whole file only if it may be Model's.
STATIC BOOLEAN
FindWant (
  IN OUT VOID       *Context,
  IN CONST DTB_PEEK *Peek
)
{
  FIND_VISIT *Find = Context;

  return !Peek->Known ||
         (Peek->HasModel && TextEqStr (UpToNul (Peek->Model), Find->Model));
}

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

    if (ReadDtbs (Fs, mDtbDirs[Dir], FindWant, FindVisit, &Find)) {
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
  UINT32          Chip = QuirksChipId ();
  CONST SOC_INFO *Soc  = SocById (Chip);

  if (!SocHasCompatibles (Soc)) {
    LogPrint (
      Log, "scan: no SoC compatible for chip id %u; showing all models", Chip
    );
    Soc = NULL;
  }
  for (UINTN Drive = 0; Drive < Count; Drive++) {
    for (UINTN Dir = 0; Dir < N_DTB_DIRS; Dir++) {
      SCAN_VISIT Scan  = {Out, Soc, 0, 0};
      UINTN      First = Out->Count;

      ReadDtbs (Drives[Drive], mDtbDirs[Dir], ScanWant, ScanVisit, &Scan);
      if (Scan.Trees) {
        LogPrint (
          Log,
          "scan: drive %llu: %llu new model(s) from %s, %llu for other SoCs",
          (UINT64)Drive, (UINT64)(Out->Count - First), mDtbDirs[Dir],
          (UINT64)Scan.Others
        );
      }
    }
  }
  SortBy (Out->Items, Out->Count, sizeof (*Out->Items), ByName, NULL);
}
