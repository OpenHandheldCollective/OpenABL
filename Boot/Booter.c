/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Booter - tries the boot plan in order and hands over to the image.
*/

#include "Boot/Booter.h"

#include "Efi/Efi.h"

#include <Guid/Fdt.h>

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Boot/Bli.h"
#include "Boot/BootUtil.h"
#include "Boot/LinuxBoot.h"
#include "Core/Bls.h"
#include "Core/BootEntry.h"
#include "Core/DeviceTree.h"
#include "Core/EfiData.h"
#include "Efi/Files.h"
#include "Efi/Handover.h"
#include "Efi/Serial.h"

// Largest devicetree file read.
#define DTB_LIMIT ((UINTN)8 << 20)

BOOLEAN
BootHostModelDevicetree (
  IN CONST BOOT_HOST *Host,
  OUT OABL_BUFFER    *Out
)
{
  *Out = (OABL_BUFFER){0};
  if (Host->ModelDevicetree && Host->ModelDevicetree (Host->Context, Out)) {
    return TRUE;
  }
  BufferFree (Out);
  return FALSE;
}

CHAR8 *
BootHostDefaultModelCompatible (IN CONST BOOT_HOST *Host)
{
  DT_INFO     Info;
  OABL_BUFFER Dtb;
  CHAR8      *Compatible = NULL;

  if (!BootHostModelDevicetree (Host, &Dtb)) {
    return NULL;
  }
  if (DtInspect (Dtb.Data, Dtb.Len, &Info)) {
    Compatible      = Info.Compatible;
    Info.Compatible = NULL;
    DtInfoFree (&Info);
  }
  BufferFree (&Dtb);
  return Compatible;
}

CHAR8 *
BootHostModelCompatible (IN CONST BOOT_HOST *Host)
{
  if (Host->ModelCompatible) {
    return Host->ModelCompatible (Host->Context);
  }
  return BootHostDefaultModelCompatible (Host);
}

STATIC VOID
HostBeforeStart (
  IN CONST BOOT_HOST *Host,
  IN EFI_HANDLE       Fs OPTIONAL,
  IN BOOLEAN          OwnDevicetree
)
{
  if (Host->BeforeStart) {
    Host->BeforeStart (Host->Context, Fs, OwnDevicetree);
  }
}

STATIC VOID
HostAfterReturn (IN CONST BOOT_HOST *Host)
{
  if (Host->AfterReturn) {
    Host->AfterReturn (Host->Context);
  }
}

STATIC VOID
HostCheckpoint (
  IN CONST BOOT_HOST *Host,
  IN CONST CHAR8     *Why
)
{
  if (Host->Checkpoint) {
    Host->Checkpoint (Host->Context, Why);
  }
}

// How starting one target went. Ran is TRUE once StartImage was called,
// and Ok or Err tell how it ended. Otherwise loading failed, Err says why
// and the next target is tried.
typedef struct _OUTCOME {
  BOOLEAN Ran;
  BOOLEAN Ok;
  CHAR8  *Err;
} OUTCOME;

// Outcome for a target that couldn't be loaded. Takes over Err.
STATIC OUTCOME
Skipped (IN CHAR8 *Err OPTIONAL)
{
  return (OUTCOME){.Err = Err};
}

VOID
BooterNew (
  OUT BOOTER          *Out,
  IN CONST CHAR8      *Info,
  IN UINT64            InitUs,
  IN CONST BOOT_SCOPE *Scope
)
{
  *Out = (BOOTER){
    .Scope  = {.Only = Scope->Only, .DefaultFiles = Scope->DefaultFiles},
    .Info   = Info,
    .InitUs = InitUs};
  if (Scope->Only && Scope->VolumeCount) {
    EFI_HANDLE *Volumes = AllocatePool (Scope->VolumeCount * sizeof (*Volumes));

    if (Volumes) {
      for (UINTN Index = 0; Index < Scope->VolumeCount; Index++) {
        Volumes[Index] = Scope->Volumes[Index];
      }
      Out->Scope.Volumes     = Volumes;
      Out->Scope.VolumeCount = Scope->VolumeCount;
    }
  }
  BootManagerNew (&Out->Manager);
  BooterRefresh (Out);
}

VOID
BooterFree (IN OUT BOOTER *Booter)
{
  BootManagerFree (&Booter->Manager);
  BootEntriesVolumesFree (&Booter->Volumes);
  BootManagerTargetsFree (&Booter->Plan);
  FreePool ((VOID *)Booter->Scope.Volumes);
  *Booter = (BOOTER){.Info = ""};
}

VOID
BooterRefresh (IN OUT BOOTER *Booter)
{
  UINTN Kept = 0;

  BootManagerTargetsFree (&Booter->Plan);
  BootEntriesVolumesFree (&Booter->Volumes);
  BootEntriesDiscover (
    BootManagerOwnImage (&Booter->Manager), Booter->Scope.Only,
    Booter->Scope.Volumes, Booter->Scope.VolumeCount,
    Booter->Scope.DefaultFiles, &Booter->Volumes
  );
  BootManagerPlan (
    &Booter->Manager, Booter->Volumes.Items, Booter->Volumes.Count,
    &Booter->Plan
  );
  if (Booter->Scope.Only) {
    // Drop Boot#### files that are not on one of the given drives.
    for (UINTN Index = 0; Index < Booter->Plan.Count; Index++) {
      BOOT_MANAGER_TARGET *Target = &Booter->Plan.Items[Index];

      if (Target->Kind == BOOT_MANAGER_TARGET_EFI && !Target->HasVolume) {
        BootManagerTargetFree (Target);
      } else {
        Booter->Plan.Items[Kept++] = *Target;
      }
    }
    Booter->Plan.Count = Kept;
  }
  for (UINTN Index = 0; Index < Booter->Plan.Count; Index++) {
    CHAR8 *Description = BooterDescribe (Booter, &Booter->Plan.Items[Index]);

    SerialLog ("plan %zu: %s", Index, Description);
    FreePool (Description);
  }
}

CHAR8 *
BooterDescribe (
  IN CONST BOOTER              *Booter,
  IN CONST BOOT_MANAGER_TARGET *Target
)
{
  CHAR8 *Text;

  if (Target->Kind == BOOT_MANAGER_TARGET_EFI) {
    CHAR8  Option[16] = "";
    CHAR8 *Path       = PathsDescribe (Target->Path.Data, Target->Path.Len);

    if (Target->HasOption) {
      SPrint (Option, sizeof (Option), "Boot%04X", (UINT32)Target->Option);
    }
    Text =
      Path ? SPrintAlloc ("%s \"%s\": %s", Option, Target->Label, Path) : NULL;
    FreePool (Path);
  } else {
    CONST BOOT_ENTRIES_VOLUME *V     = &Booter->Volumes.Items[Target->Volume];
    CONST BOOT_ENTRY          *Entry = &V->Entries.Items[Target->Entry];
    CHAR8                     *Drive = BootEntriesVolumeDescribe (V);

    Text = Drive ? SPrintAlloc (
                     "%s %s \"%s\" on %s", BootEntrySourceDebug (Entry->Source),
                     Entry->Id, Entry->Title, Drive
                   )
                 : NULL;
    FreePool (Drive);
  }
  return Text;
}

// Reads the entry's own devicetree (BLS devicetree or extlinux FDT).
STATIC BOOLEAN
OwnDevicetree (
  IN EFI_HANDLE               Fs,
  IN CONST BOOT_ENTRY_ACTION *Action,
  OUT OABL_BUFFER            *Out
)
{
  *Out = (OABL_BUFFER){0};
  if (!Action->Devicetree) {
    return FALSE;
  }
  if (FilesRead (Fs, Action->Devicetree, DTB_LIMIT, Out) &&
      DtInspect (Out->Data, Out->Len, NULL)) {
    SerialLog ("  devicetree from the entry: %s", Action->Devicetree);
    return TRUE;
  }
  BufferFree (Out);
  SerialLog (
    "  the entry's devicetree %s is missing or invalid; using the model's",
    Action->Devicetree
  );
  return FALSE;
}

STATIC INT32
Trimmed (IN CONST CHAR8 *Path)
{
  UINTN Length = AsciiStrLen (Path);

  while (Length && Path[Length - 1] == '\\') {
    Length--;
  }
  return (INT32)Length;
}

// Finds the .dtb in Dir, or one directory down (such as qcom), whose first
// compatible is Compat, as U-Boot finds it under FDTDIR through fdtfile.
// Caller frees PathOut and Out.
STATIC BOOLEAN
FindDtb (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Dir,
  IN CONST CHAR8  *Compat,
  OUT CHAR8      **PathOut,
  OUT OABL_BUFFER *Out
)
{
  FILES_DIR_ENTRIES Top          = {0};
  struct OABL_VEC (CHAR8 *) Dirs = {0};
  BOOLEAN Found                  = FALSE;
  CHAR8  *DirCopy                = AsciiStrDup (Dir);

  if (!DirCopy || !VecPush (&Dirs, DirCopy)) {
    FreePool (DirCopy);
    return FALSE;
  }
  FilesList (Fs, Dir, &Top);
  for (UINTN Index = 0; Index < Top.Count; Index++) {
    CHAR8 *Sub;

    if (!Top.Items[Index].IsDir) {
      continue;
    }
    Sub = SPrintAlloc ("%.*s\\%s", Trimmed (Dir), Dir, Top.Items[Index].Name);
    if (Sub && !VecPush (&Dirs, Sub)) {
      FreePool (Sub);
    }
  }
  FilesEntriesFree (&Top);
  for (UINTN Index = 0; Index < Dirs.Count && !Found; Index++) {
    CONST CHAR8      *Directory = Dirs.Items[Index];
    FILES_DIR_ENTRIES List      = {0};

    FilesList (Fs, Directory, &List);
    for (UINTN Index2 = 0; Index2 < List.Count && !Found; Index2++) {
      DT_INFO     Info;
      OABL_BUFFER Data;
      CHAR8      *Path;

      if (List.Items[Index2].IsDir ||
          !TextEndsWithNoCase (TextOf (List.Items[Index2].Name), ".dtb")) {
        continue;
      }
      Path = SPrintAlloc (
        "%.*s\\%s", Trimmed (Directory), Directory, List.Items[Index2].Name
      );
      if (!Path) {
        continue;
      }
      if (FilesRead (Fs, Path, DTB_LIMIT, &Data)) {
        if (DtInspect (Data.Data, Data.Len, &Info)) {
          Found = Info.Compatible && AsciiStrEq (Info.Compatible, Compat);
          DtInfoFree (&Info);
        }
        if (Found) {
          *PathOut = Path;
          *Out     = Data;
          break;
        }
        BufferFree (&Data);
      }
      FreePool (Path);
    }
    FilesEntriesFree (&List);
  }
  for (UINTN Index = 0; Index < Dirs.Count; Index++) {
    FreePool (Dirs.Items[Index]);
  }
  VecFree (&Dirs);
  return Found;
}

// Gets the devicetree for an entry without its own. Looks for the model's
// in the extlinux FDTDIR first, then asks the host.
STATIC BOOLEAN
ModelDevicetreeFor (
  IN CONST BOOT_HOST         *Host,
  IN EFI_HANDLE               Fs,
  IN CONST BOOT_ENTRY_ACTION *Action,
  OUT OABL_BUFFER            *Out
)
{
  CONST CHAR8 *Fdtdir =
    Action->Kind == BOOT_ENTRY_ACTION_LINUX ? Action->FdtDir : NULL;
  // Ask even without an FDTDIR, so the host's ModelCompatible always runs.
  CHAR8 *Compat = BootHostModelCompatible (Host);

  if (Fdtdir && Compat) {
    CHAR8 *Path = NULL;

    if (FindDtb (Fs, Fdtdir, Compat, &Path, Out)) {
      SerialLog ("  devicetree for %s from FDTDIR: %s", Compat, Path);
      FreePool (Path);
      FreePool (Compat);
      return TRUE;
    }
    SerialLog (
      "  no devicetree for %s in %s; using the model's", Compat, Fdtdir
    );
  }
  FreePool (Compat);
  return BootHostModelDevicetree (Host, Out);
}

// Installs Dtb, if any, and starts the image. Frees Dtb. The host's
// BeforeStart must already have run.
STATIC OUTCOME
Run (
  IN CONST BOOT_HOST *Host,
  IN EFI_HANDLE       Image,
  IN OUT OABL_BUFFER *Dtb OPTIONAL,
  IN CONST CHAR8     *What
)
{
  OUTCOME    Outcome  = {.Ran = TRUE};
  UINTN      ExitSize = 0;
  CHAR16    *ExitData = NULL;
  EFI_STATUS Status;

  if (Dtb) {
    CONST CHAR8 *Err = HandoffInstallDevicetree (Dtb->Data, Dtb->Len);

    if (Err) {
      SerialLog ("devicetree handoff failed: %s", Err);
    }
    BufferFree (Dtb);
  }
  SerialLog ("starting %s", What);
  HostCheckpoint (Host, "starting an image");
#ifndef OABL_DEBUG
  if (gST->ConOut) {
    gST->ConOut->ClearScreen (gST->ConOut);
  }
#endif
  Status = gBS->StartImage (Image, &ExitSize, &ExitData);
  // The caller of StartImage frees the exit data.
  if (ExitData) {
    gBS->FreePool (ExitData);
  }
  if (Status == EFI_SUCCESS || Status == EFI_ABORTED) {
    Outcome.Ok = TRUE;
  } else {
    Outcome.Err = SPrintAlloc (
      "The image exited with %s",
      StatusDebug (Status, (CHAR8[BOOT_STATUS_DEBUG_SIZE]){0})
    );
  }
  HostAfterReturn (Host);
  return Outcome;
}

// Loads the image at a full device path, as the boot manager does.
STATIC BOOLEAN
LoadFile (
  IN CONST OABL_BUFFER *Path,
  OUT EFI_HANDLE       *Image,
  OUT CHAR8           **Err
)
{
  EFI_HANDLE Handle = NULL;
  EFI_STATUS Status;
  CHAR8     *Description;

  *Image = NULL;
  *Err   = NULL;
  if (!DevicePathValid (Path->Data, Path->Len)) {
    *Err = AsciiStrDup ("bad device path");
    return FALSE;
  }
  Status = gBS->LoadImage (
    TRUE, gImageHandle, (EFI_DEVICE_PATH_PROTOCOL *)Path->Data, NULL, 0, &Handle
  );
  if (Status != EFI_SUCCESS) {
    Description = PathsDescribe (Path->Data, Path->Len);
    *Err        = SPrintAlloc (
      "LoadImage %s: %s", Description,
      StatusDebug (Status, (CHAR8[BOOT_STATUS_DEBUG_SIZE]){0})
    );
    FreePool (Description);
    return FALSE;
  }
  *Image = Handle;
  return TRUE;
}

STATIC OUTCOME
StartEntry (
  IN OUT BOOTER      *Booter,
  IN CONST BOOT_HOST *Host,
  IN UINTN            VolumeIndex,
  IN UINTN            EntryIndex
);

STATIC OUTCOME
Start (
  IN OUT BOOTER                *Booter,
  IN CONST BOOT_MANAGER_TARGET *Target,
  IN CONST BOOT_HOST           *Host
)
{
  OABL_BUFFER Dtb;
  OUTCOME     Outcome;
  EFI_HANDLE  Image;
  CHAR8      *Err;
  CHAR8      *What;
  BOOLEAN     HasDtb;

  if (Target->Kind == BOOT_MANAGER_TARGET_ENTRY) {
    return StartEntry (Booter, Host, Target->Volume, Target->Entry);
  }
  if (!LoadFile (&Target->Path, &Image, &Err)) {
    return Skipped (Err);
  }
  // LoadOptions point into Target->Options, which outlive the image.
  if (!LinuxBootSetOptions (Image, &Target->Options, &Err)) {
    gBS->UnloadImage (Image);
    return Skipped (Err);
  }
  BootManagerSetBootCurrent (Target->HasOption, Target->Option);
  HostBeforeStart (
    Host, Target->HasVolume ? Booter->Volumes.Items[Target->Volume].Fs : NULL,
    FALSE
  );
  HasDtb  = BootHostModelDevicetree (Host, &Dtb);
  What    = PathsDescribe (Target->Path.Data, Target->Path.Len);
  Outcome = Run (Host, Image, HasDtb ? &Dtb : NULL, What ? What : "?");
  FreePool (What);
  return Outcome;
}

// Borrowed ids of the Boot Loader Specification entries.
typedef struct _ID_LIST
OABL_VEC (CHAR8 *) ID_LIST;

// Starts a Boot Loader Specification entry or extlinux label. Counts the
// try, loads the image, sets the Boot Loader Interface variables, then
// installs the devicetree and runs it.
STATIC OUTCOME
StartEntry (
  IN OUT BOOTER      *Booter,
  IN CONST BOOT_HOST *Host,
  IN UINTN            VolumeIndex,
  IN UINTN            EntryIndex
)
{
  CONST BOOT_ENTRIES_VOLUME *V        = &Booter->Volumes.Items[VolumeIndex];
  EFI_HANDLE                 Fs       = V->Fs;
  EFI_HANDLE                 Image    = NULL;
  OABL_BYTES                 Dev      = BufferBytes (&V->Dev);
  CONST CHAR8               *PartUuid = V->PartUuid;
  LINUX_BOOT_INITRD         *Initrd   = NULL;
  OABL_BUFFER                Options  = {0};
  OABL_BUFFER                Dtb;
  ID_LIST                    Ids = {0};
  BOOT_ENTRY                 Entry;
  OUTCOME                    Outcome;
  CHAR8                     *File;
  CHAR8                     *CountPath = NULL;
  CHAR8                     *Err       = NULL;
  CHAR8                     *ImagePath;
  BOOLEAN                    Own;
  BOOLEAN                    HasDtb;

  if (!BootEntryClone (&V->Entries.Items[EntryIndex], &Entry)) {
    return Skipped (AsciiStrDup ("out of memory"));
  }
  SerialLog (
    "entry %s \"%s\" (%s)", Entry.Id, Entry.Title,
    BootEntrySourceDebug (Entry.Source)
  );

  // Automatic Boot Assessment. Record the try before booting.
  File = AsciiStrDup (Entry.File);
  if (!File) {
    BootEntryFree (&Entry);
    return Skipped (AsciiStrDup ("out of memory"));
  }
  if (Entry.HasCounter) {
    BLS_COUNTER Next = BlsCounterBumped (Entry.Counter);
    CHAR8      *Name = BlsCountedName (Entry.Id, Next);
    EFI_STATUS  Status =
      Name ? FilesRename (Fs, Entry.File, Name) : EFI_OUT_OF_RESOURCES;

    if (Status == EFI_SUCCESS) {
      OABL_TEXT Parent = BootEntryParent (Entry.File);
      CHAR8    *Renamed =
        TextEqStr (Parent, "\\")
          ? SPrintAlloc ("\\%s", Name)
          : SPrintAlloc ("%.*s\\%s", (INT32)Parent.Len, Parent.Ptr, Name);

      if (Renamed) {
        BOOT_ENTRY *Stored =
          &Booter->Volumes.Items[VolumeIndex].Entries.Items[EntryIndex];
        CHAR8 *StoredFile = AsciiStrDup (Renamed);

        FreePool (File);
        File = Renamed;
        SerialLog ("  boot counting: %s -> %s", Entry.File, File);
        if (StoredFile) {
          FreePool (Stored->File);
          Stored->File = StoredFile;
        }
        Stored->HasCounter = TRUE;
        Stored->Counter    = Next;
        CountPath          = AsciiStrDup (File);
      }
    } else {
      SerialLog (
        "  boot counting: renaming %s failed (%s); booting anyway", Entry.File,
        StatusDebug (Status, (CHAR8[BOOT_STATUS_DEBUG_SIZE]){0})
      );
    }
    FreePool (Name);
  }

  if (Entry.Action.Kind == BOOT_ENTRY_ACTION_LINUX) {
    if (Entry.Action.Overlays.Count) {
      OABL_STRING Line = {0};

      StringAppend (
        &Line, "  devicetree overlays aren't applied yet; ignoring "
      );
      StringDebugStrs (
        &Line, Entry.Action.Overlays.Items, Entry.Action.Overlays.Count
      );
      SerialLog ("%s", StringGet (&Line));
      StringFree (&Line);
    }
    if (!LinuxBootLoad (Fs, Dev, Entry.Action.Kernel, &Image, &Err)) {
      Outcome = Skipped (Err);
      goto Done;
    }
    if (!LinuxBootInstallInitrd (Fs, &Entry.Action.Initrds, &Initrd, &Err)) {
      gBS->UnloadImage (Image);
      Outcome = Skipped (Err);
      goto Done;
    }
    SerialLog (
      "  kernel %s with \"%s\"", Entry.Action.Kernel, Entry.Action.Cmdline
    );
    if (!LinuxBootTextOptions (Entry.Action.Cmdline, &Options)) {
      gBS->UnloadImage (Image);
      Outcome = Skipped (AsciiStrDup ("out of memory for the load options"));
      goto Done;
    }
  } else {
    // A UKI is the entry file itself, which boot counting may just have
    // renamed.
    CONST CHAR8 *Path =
      AsciiStrEq (Entry.Action.Path, Entry.File) ? File : Entry.Action.Path;
    OABL_BUFFER Node = {0};
    OABL_BUFFER Full = {0};
    BOOLEAN     Loaded;

    if (UefiFilePathNode (Path, &Node)) {
      UefiJoin ((OABL_BYTES[]){Dev, BufferBytes (&Node)}, 2, &Full);
    }
    Loaded = LoadFile (&Full, &Image, &Err);
    BufferFree (&Full);
    BufferFree (&Node);
    if (!Loaded) {
      Outcome = Skipped (Err);
      goto Done;
    }
    if (Entry.Action.Options &&
        !LinuxBootTextOptions (Entry.Action.Options, &Options)) {
      gBS->UnloadImage (Image);
      Outcome = Skipped (AsciiStrDup ("out of memory for the load options"));
      goto Done;
    }
  }
  if (!LinuxBootSetOptions (Image, &Options, &Err)) {
    gBS->UnloadImage (Image);
    Outcome = Skipped (Err);
    goto Done;
  }
  BootManagerSetBootCurrent (FALSE, 0);
  for (UINTN Index = 0; Index < Booter->Volumes.Count; Index++) {
    for (UINTN Index2 = 0; Index2 < Booter->Volumes.Items[Index].Entries.Count;
         Index2++) {
      if (BootEntryIsBls (
            &Booter->Volumes.Items[Index].Entries.Items[Index2]
          )) {
        VecPush (&Ids, Booter->Volumes.Items[Index].Entries.Items[Index2].Id);
      }
    }
  }
  ImagePath = UefiFilePathText (
    Booter->Manager.OwnImage.Data, Booter->Manager.OwnImage.Len
  );
  BliReport (&(BLI_STARTED){
    .LoaderInfo = Booter->Info,
    .InitUs     = Booter->InitUs,
    .ImagePath  = ImagePath,
    .PartUuid   = PartUuid,
    .Entries    = Ids.Items,
    .EntryCount = Ids.Count,
    .Selected   = Entry.Id,
    .CountPath  = CountPath,
  });
  FreePool (ImagePath);
  VecFree (&Ids);
  // Look for the entry's own devicetree first, so the platform doesn't
  // install the model's only to have it replaced.
  Own = OwnDevicetree (Fs, &Entry.Action, &Dtb);
  HostBeforeStart (Host, Fs, Own);
  HasDtb  = Own || ModelDevicetreeFor (Host, Fs, &Entry.Action, &Dtb);
  Outcome = Run (Host, Image, HasDtb ? &Dtb : NULL, Entry.Title);
Done:
  // The load options and initrd must stay until the image has run.
  BufferFree (&Options);
  LinuxBootInitrdFree (Initrd);
  FreePool (CountPath);
  FreePool (File);
  BootEntryFree (&Entry);
  return Outcome;
}

BOOT_FAILURE
BooterBoot (
  IN OUT BOOTER      *Booter,
  IN CONST BOOT_HOST *Host,
  IN BOOLEAN          HasOnly,
  IN UINTN            Only,
  OUT CHAR8         **Why OPTIONAL
)
{
  UINTN   First   = 0;
  UINTN   End     = Booter->Plan.Count;
  CHAR8  *LastErr = NULL;
  BOOLEAN AnyErr  = FALSE;

  if (Why) {
    *Why = NULL;
  }
#ifdef OABL_DEBUG
  // The boot's steps on screen too, to see where one stops.
  SerialToScreen ();
#endif
  if (HasOnly) {
    First = Only;
    End   = Only < Booter->Plan.Count ? Only + 1 : Only;
  }
  if (First >= End) {
    return BOOT_FAILURE_NOTHING_FOUND;
  }
  for (UINTN Index = First; Index < End; Index++) {
    CONST BOOT_MANAGER_TARGET *Target  = &Booter->Plan.Items[Index];
    OUTCOME                    Outcome = Start (Booter, Target, Host);
    CHAR8                     *Description;

    if (Outcome.Ran) {
      FreePool (LastErr);
      if (Outcome.Ok) {
        return BOOT_FAILURE_OK;
      }
      if (Why) {
        *Why = Outcome.Err;
      } else {
        FreePool (Outcome.Err);
      }
      return BOOT_FAILURE_FAILED;
    }
    Description = BooterDescribe (Booter, Target);
    SerialLog ("  %s: %s", Description, Outcome.Err);
    FreePool (Description);
    FreePool (LastErr);
    LastErr = Outcome.Err;
    AnyErr  = TRUE;
  }
  if (!AnyErr) {
    return BOOT_FAILURE_NOTHING_FOUND;
  }
  if (Why) {
    *Why = LastErr;
  } else {
    FreePool (LastErr);
  }
  return BOOT_FAILURE_FAILED;
}

// EBBR hand-over

// FDT header size, and the largest installed devicetree copied (as
// DTB_LIMIT).
#define FDT_HEADER_SIZE     40
#define INSTALLED_DTB_LIMIT ((UINT32)8 << 20)

// Returns the configuration table for Guid, or NULL.
STATIC VOID *
HasTable (IN CONST EFI_GUID *Guid)
{
  for (UINTN Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    if (CompareGuid (&gST->ConfigurationTable[Index].VendorGuid, Guid)) {
      return gST->ConfigurationTable[Index].VendorTable;
    }
  }
  return NULL;
}

STATIC VOID
LogToSink (
  IN VOID        *Context,
  IN CONST CHAR8 *Line
)
{
  (VOID)Context;
  SerialLog ("%s", Line);
}

CONST CHAR8 *
HandoffInstallDevicetree (
  IN CONST UINT8 *Dtb,
  IN UINTN        Size
)
{
  CONST LOG_SINK Log = {LogToSink, NULL};
  CONST VOID    *At  = NULL;
  UINTN          Len = 0;
  CONST CHAR8   *Err = HandoverInstallDevicetree (Dtb, Size, Log, &Len, &At);

  if (Err) {
    return Err;
  }
  SerialLog (
    "devicetree installed: %zu bytes in ACPI-reclaim memory at %p", Len, At
  );
  return NULL;
}

BOOLEAN
HandoffInstalledDevicetree (OUT OABL_BUFFER *Out)
{
  CONST UINT8 *Blob = HasTable (&gFdtTableGuid);
  UINT32       Total;

  *Out = (OABL_BUFFER){0};
  if (!Blob) {
    return FALSE;
  }
  // FDT magic, then the big-endian total size at offset 4.
  if (Blob[0] != 0xd0 || Blob[1] != 0x0d || Blob[2] != 0xfe ||
      Blob[3] != 0xed) {
    return FALSE;
  }
  Total = ReadBe32 (Blob + 4);
  // Don't copy a blob whose size can't be a devicetree's.
  if (Total < FDT_HEADER_SIZE || Total > INSTALLED_DTB_LIMIT) {
    return FALSE;
  }
  return BufferFrom (Out, Blob, Total);
}
