/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootManager - decides what to boot, and in what order.
*/

#include "Boot/BootManager.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Boot/Bli.h"
#include "Boot/BootUtil.h"
#include "Core/Bls.h"
#include "Core/EfiData.h"
#include "Efi/Files.h"
#include "Efi/Serial.h"

VOID
BootManagerTargetFree (IN OUT BOOT_MANAGER_TARGET *Target)
{
  FreePool (Target->Label);
  BufferFree (&Target->Path);
  BufferFree (&Target->Options);
  *Target = (BOOT_MANAGER_TARGET){0};
}

VOID
BootManagerTargetsFree (IN OUT BOOT_MANAGER_TARGETS *Targets)
{
  for (UINTN Index = 0; Index < Targets->Count; Index++) {
    BootManagerTargetFree (&Targets->Items[Index]);
  }
  VecFree (Targets);
}

STATIC BOOLEAN
GetGlobal (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
)
{
  return GetVariable (Name, &gEfiGlobalVariableGuid, Out) == EFI_SUCCESS;
}

VOID
BootManagerNew (OUT BOOT_MANAGER *Out)
{
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
  OABL_BUFFER                Dev         = {0};
  UINTN                      FileSize;

  *Out = (BOOT_MANAGER){0};
  if (OpenExclusive (
        gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage
      ) != EFI_SUCCESS ||
      !LoadedImage) {
    return;
  }
  if (LoadedImage->DeviceHandle &&
      PathsHandlePath (LoadedImage->DeviceHandle, &Dev)) {
    FileSize = DevicePathSize (LoadedImage->FilePath);
    if (FileSize) {
      UefiJoin (
        (OABL_BYTES[]){BufferBytes (&Dev),
                       BytesOf (LoadedImage->FilePath, FileSize)},
        2, &Out->OwnImage
      );
    }
  }
  BufferFree (&Dev);
  CloseExclusive (gImageHandle, &gEfiLoadedImageProtocolGuid);
}

VOID
BootManagerFree (IN OUT BOOT_MANAGER *Manager)
{
  BufferFree (&Manager->OwnImage);
}

STATIC BOOLEAN
IsSelf (
  IN CONST BOOT_MANAGER *Manager,
  IN CONST OABL_BUFFER  *Path
)
{
  return Manager->OwnImage.Len &&
         UefiSameNodes (
           Path->Data, Path->Len, Manager->OwnImage.Data, Manager->OwnImage.Len
         );
}

// Appends an EFI target, taking over Path. Drops it if out of memory.
STATIC VOID
PushFile (
  IN OUT BOOT_MANAGER_TARGETS *Out,
  IN BOOLEAN                   HasOption,
  IN UINT16                    Option,
  IN CONST CHAR8              *Label,
  IN OUT OABL_BUFFER          *Path,
  IN OABL_BYTES                Options,
  IN BOOLEAN                   HasVolume,
  IN UINTN                     Volume
)
{
  BOOT_MANAGER_TARGET Target = {
    .Kind      = BOOT_MANAGER_TARGET_EFI,
    .HasOption = HasOption,
    .Option    = Option,
    .Label     = AsciiStrDup (Label),
    .Path      = *Path,
    .HasVolume = HasVolume,
    .Volume    = Volume,
  };

  *Path = (OABL_BUFFER){0};
  if (!Target.Label ||
      !BufferFrom (&Target.Options, Options.Ptr, Options.Len) ||
      !VecPush (Out, Target)) {
    BootManagerTargetFree (&Target);
  }
}

// Appends drive Volume's entries, then its default file.
STATIC VOID
Drive (
  IN CONST BOOT_ENTRIES_VOLUME *Volumes,
  IN UINTN                      Volume,
  IN OUT BOOT_MANAGER_TARGETS  *Out
)
{
  CONST BOOT_ENTRIES_VOLUME *V    = &Volumes[Volume];
  OABL_BUFFER                Node = {0};
  OABL_BUFFER                Path = {0};

  for (UINTN Entry = 0; Entry < V->Entries.Count; Entry++) {
    VecPush (
      Out, (BOOT_MANAGER_TARGET){
             .Kind      = BOOT_MANAGER_TARGET_ENTRY,
             .HasVolume = TRUE,
             .Volume    = Volume,
             .Entry     = Entry,
           }
    );
  }
  if (V->DefaultFile) {
    if (UefiFilePathNode (UEFI_DEFAULT_BOOT_FILE, &Node) &&
        UefiJoin (
          (OABL_BYTES[]){BufferBytes (&V->Dev), BufferBytes (&Node)}, 2, &Path
        )) {
      PushFile (
        Out, FALSE, 0, "default boot file", &Path, BytesOf (NULL, 0), TRUE,
        Volume
      );
    }
    BufferFree (&Node);
  }
}

// Appends a whole drive for Boot<Option>, as Drive does. The default file
// gets the option's number, description and data.
STATIC VOID
Whole (
  IN CONST BOOT_ENTRIES_VOLUME *Volumes,
  IN UINTN                      Volume,
  IN UINT16                     Option,
  IN CONST UEFI_LOAD_OPTION    *LoadOption,
  IN OUT BOOT_MANAGER_TARGETS  *Out
)
{
  UINTN Start = Out->Count;

  Drive (Volumes, Volume, Out);
  for (UINTN Index = Start; Index < Out->Count; Index++) {
    BOOT_MANAGER_TARGET *Target  = &Out->Items[Index];
    OABL_BUFFER          Options = {0};
    CHAR8               *Label;

    if (Target->Kind != BOOT_MANAGER_TARGET_EFI) {
      continue;
    }
    Label = AsciiStrDup (LoadOption->Description);
    if (!Label ||
        !BufferFrom (
          &Options, LoadOption->OptionalData.Data, LoadOption->OptionalData.Len
        )) {
      // Out of memory. Keep the drive's own label and options.
      FreePool (Label);
      continue;
    }
    Target->HasOption = TRUE;
    Target->Option    = Option;
    BufferFree (&Target->Options);
    Target->Options = Options;
    FreePool (Target->Label);
    Target->Label = Label;
  }
}

// Gets the last node of Path. Returns FALSE if Path has no nodes.
STATIC BOOLEAN
LastNode (
  IN CONST OABL_BUFFER *Path,
  OUT UEFI_NODE        *Last
)
{
  UEFI_NODE Node;
  UINTN     At  = 0;
  BOOLEAN   Any = FALSE;

  while (UefiNextNode (Path->Data, Path->Len, &At, &Node)) {
    *Last = Node;
    Any   = TRUE;
  }
  return Any;
}

STATIC BOOLEAN
BytesEq (
  IN OABL_BYTES A,
  IN OABL_BYTES B
)
{
  return A.Len == B.Len &&
         (A.Len == 0 || CompareMem (A.Ptr, B.Ptr, A.Len) == 0);
}

// Appends the targets for load option Boot<Option>.
STATIC VOID
ResolveOption (
  IN CONST BOOT_MANAGER        *Manager,
  IN UINT16                     Option,
  IN CONST BOOT_ENTRIES_VOLUME *Volumes,
  IN UINTN                      VolumeCount,
  IN OUT BOOT_MANAGER_TARGETS  *Out
)
{
  UEFI_LOAD_OPTION LoadOption;
  OABL_BUFFER      Raw = {0};
  UEFI_PATH_KIND   Kind;
  CONST UINT8     *DevPath;
  UINTN            DevPathLen;
  UINTN            Start = Out->Count;
  CHAR8            Name[16];
  BOOLEAN          Parsed;

  (VOID)Manager;
  UefiBootVarName (Option, Name, sizeof (Name));
  Parsed = GetGlobal (Name, &Raw) &&
           UefiLoadOptionParse (Raw.Data, Raw.Len, &LoadOption);
  BufferFree (&Raw);
  if (!Parsed) {
    SerialLog ("  %s: missing or malformed", Name);
    return;
  }
  if (!UefiLoadOptionIsActive (&LoadOption)) {
    SerialLog ("  %s \"%s\": inactive", Name, LoadOption.Description);
    UefiLoadOptionFree (&LoadOption);
    return;
  }
  DevPath    = LoadOption.DevicePath.Data;
  DevPathLen = LoadOption.DevicePath.Len;
  Kind       = UefiClassify (DevPath, DevPathLen);
  switch (Kind) {
  case UEFI_PATH_FIRMWARE:
    break;
  case UEFI_PATH_FULL_FILE: {
    OABL_BUFFER Path      = {0};
    BOOLEAN     HasVolume = FALSE;
    UINTN       Volume    = 0;

    for (UINTN Index = 0; Index < VolumeCount && !HasVolume; Index++) {
      if (UefiStartsWith (
            DevPath, DevPathLen, Volumes[Index].Dev.Data, Volumes[Index].Dev.Len
          )) {
        HasVolume = TRUE;
        Volume    = Index;
      }
    }
    if (BufferFrom (&Path, DevPath, DevPathLen)) {
      PushFile (
        Out, TRUE, Option, LoadOption.Description, &Path,
        BufferBytes (&LoadOption.OptionalData), HasVolume, Volume
      );
    }
    break;
  }
  case UEFI_PATH_SHORT_FORM_HARD_DRIVE: {
    UEFI_NODE Hd;
    UEFI_NODE Last;
    UINTN     At = 0;
    CHAR8    *Text;
    BOOLEAN   HasFile;

    // The first node is the partition's HD() node.
    if (!UefiNextNode (DevPath, DevPathLen, &At, &Hd)) {
      break;
    }
    Text    = UefiFilePathText (DevPath, DevPathLen);
    HasFile = Text != NULL;
    FreePool (Text);
    for (UINTN Index = 0; Index < VolumeCount; Index++) {
      if (!LastNode (&Volumes[Index].Dev, &Last) ||
          !BytesEq (Last.Bytes, Hd.Bytes)) {
        continue;
      }
      if (HasFile) {
        // The drive's path, then the option's nodes after the partition.
        OABL_BUFFER Path = {0};

        if (UefiJoin (
              (OABL_BYTES[]){BufferBytes (&Volumes[Index].Dev),
                             BytesOf (DevPath + At, DevPathLen - At)},
              2, &Path
            )) {
          PushFile (
            Out, TRUE, Option, LoadOption.Description, &Path,
            BufferBytes (&LoadOption.OptionalData), TRUE, Index
          );
        }
      } else {
        Whole (Volumes, Index, Option, &LoadOption, Out);
      }
    }
    break;
  }
  case UEFI_PATH_SHORT_FORM_FILE: {
    CHAR8 *Text = UefiFilePathText (DevPath, DevPathLen);

    for (UINTN Index = 0; Index < VolumeCount; Index++) {
      OABL_BUFFER Path = {0};

      if (!FilesExists (Volumes[Index].Fs, Text ? Text : "")) {
        continue;
      }
      if (UefiJoin (
            (OABL_BYTES[]){BufferBytes (&Volumes[Index].Dev),
                           BytesOf (DevPath, DevPathLen)},
            2, &Path
          )) {
        PushFile (
          Out, TRUE, Option, LoadOption.Description, &Path,
          BufferBytes (&LoadOption.OptionalData), TRUE, Index
        );
      }
    }
    FreePool (Text);
    break;
  }
  case UEFI_PATH_DEVICE_ONLY:
    for (UINTN Index = 0; Index < VolumeCount; Index++) {
      if (UefiStartsWith (
            Volumes[Index].Dev.Data, Volumes[Index].Dev.Len, DevPath, DevPathLen
          )) {
        Whole (Volumes, Index, Option, &LoadOption, Out);
      }
    }
    break;
  }
  SerialLog (
    "  %s \"%s\": %s, %zu target(s)", Name, LoadOption.Description,
    UefiPathKindName (Kind), Out->Count - Start
  );
  UefiLoadOptionFree (&LoadOption);
}

// Appends the first entry, in drive order, that Match accepts with Arg.
STATIC VOID
FindEntry (
  IN CONST BOOT_ENTRIES_VOLUME *Volumes,
  IN UINTN                      VolumeCount,
  IN BOOLEAN (*Match) (
    IN CONST BOOT_ENTRY *Entry,
    IN CONST CHAR8      *Arg
  ),
  IN CONST CHAR8              *Arg,
  IN OUT BOOT_MANAGER_TARGETS *Out
)
{
  for (UINTN Volume = 0; Volume < VolumeCount; Volume++) {
    for (UINTN Entry = 0; Entry < Volumes[Volume].Entries.Count; Entry++) {
      if (Match (&Volumes[Volume].Entries.Items[Entry], Arg)) {
        VecPush (
          Out, (BOOT_MANAGER_TARGET){
                 .Kind      = BOOT_MANAGER_TARGET_ENTRY,
                 .HasVolume = TRUE,
                 .Volume    = Volume,
                 .Entry     = Entry,
               }
        );
        return;
      }
    }
  }
}

STATIC BOOLEAN
IdIs (
  IN CONST BOOT_ENTRY *Entry,
  IN CONST CHAR8      *Id
)
{
  return AsciiStrEq (Entry->Id, Id);
}

STATIC BOOLEAN
GoodAndMatches (
  IN CONST BOOT_ENTRY *Entry,
  IN CONST CHAR8      *Pattern
)
{
  return !BootEntryIsBad (Entry) && BlsGlob (Pattern, Entry->Id);
}

// Returns TRUE if Kept already holds Target. EFI targets compare by device
// path, entries by drive and index.
STATIC BOOLEAN
Seen (
  IN CONST BOOT_MANAGER_TARGET *Kept,
  IN UINTN                      Count,
  IN CONST BOOT_MANAGER_TARGET *Target
)
{
  for (UINTN Index = 0; Index < Count; Index++) {
    CONST BOOT_MANAGER_TARGET *Other = &Kept[Index];

    if (Other->Kind != Target->Kind) {
      continue;
    }
    if (Target->Kind == BOOT_MANAGER_TARGET_EFI) {
      if (UefiSameNodes (
            Other->Path.Data, Other->Path.Len, Target->Path.Data,
            Target->Path.Len
          )) {
        return TRUE;
      }
    } else if (Other->Volume == Target->Volume &&
               Other->Entry == Target->Entry) {
      return TRUE;
    }
  }
  return FALSE;
}

VOID
BootManagerPlan (
  IN CONST BOOT_MANAGER        *Manager,
  IN CONST BOOT_ENTRIES_VOLUME *Volumes,
  IN UINTN                      VolumeCount,
  OUT BOOT_MANAGER_TARGETS     *Out
)
{
  UEFI_U16_LIST Order = {0};
  OABL_BUFFER   Data  = {0};
  OABL_STRING   Line  = {0};
  CHAR8        *Wanted;
  UINTN         Kept = 0;

  *Out = (BOOT_MANAGER_TARGETS){0};
  if (GetGlobal ("BootNext", &Data) && Data.Len == 2) {
    UINT16 Next = ReadLe16 (Data.Data);

    // The boot manager deletes BootNext before trying it.
    DeleteVariable ("BootNext", &gEfiGlobalVariableGuid);
    SerialLog ("BootNext = Boot%04X", (UINT32)Next);
    ResolveOption (Manager, Next, Volumes, VolumeCount, Out);
  }
  BufferFree (&Data);
  Wanted = BliTake ("LoaderEntryOneShot");
  if (Wanted) {
    FindEntry (Volumes, VolumeCount, IdIs, Wanted, Out);
  }
  FreePool (Wanted);
  Wanted = BliGet ("LoaderEntryDefault");
  if (Wanted) {
    CHAR8 *Quoted = DebugStr (Wanted);

    SerialLog ("LoaderEntryDefault = %s", Quoted);
    FreePool (Quoted);
    FindEntry (Volumes, VolumeCount, GoodAndMatches, Wanted, Out);
  }
  FreePool (Wanted);
  if (GetGlobal ("BootOrder", &Data)) {
    UefiParseU16List (Data.Data, Data.Len, &Order);
  }
  BufferFree (&Data);
  StringAppendChar (&Line, '[');
  for (UINTN Index = 0; Index < Order.Count; Index++) {
    StringPrint (
      &Line, "%s%04X", Index ? ", " : "", (UINT32)Order.Items[Index]
    );
  }
  StringAppendChar (&Line, ']');
  SerialLog ("BootOrder = %s", StringGet (&Line));
  StringFree (&Line);
  for (UINTN Index = 0; Index < Order.Count; Index++) {
    ResolveOption (Manager, Order.Items[Index], Volumes, VolumeCount, Out);
  }
  VecFree (&Order);
  for (UINTN Index = 0; Index < VolumeCount; Index++) {
    Drive (Volumes, Index, Out);
  }

  // Keep only the first copy of each target, and never start ourselves.
  for (UINTN Index = 0; Index < Out->Count; Index++) {
    BOOT_MANAGER_TARGET *Target = &Out->Items[Index];
    BOOLEAN              Dup    = (Target->Kind == BOOT_MANAGER_TARGET_EFI &&
                                   IsSelf (Manager, &Target->Path)) ||
                                  Seen (Out->Items, Kept, Target);

    if (Dup) {
      BootManagerTargetFree (Target);
    } else {
      Out->Items[Kept++] = *Target;
    }
  }
  Out->Count = Kept;
}

VOID
BootManagerSetBootCurrent (
  IN BOOLEAN HasOption,
  IN UINT16  Option
)
{
  UINT8 Bytes[2];

  if (HasOption) {
    WriteLe16 (Bytes, Option);
    SetVariable (
      "BootCurrent", &gEfiGlobalVariableGuid,
      EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS, Bytes,
      sizeof (Bytes)
    );
  } else {
    DeleteVariable ("BootCurrent", &gEfiGlobalVariableGuid);
  }
}

// EFI_OS_INDICATIONS_BOOT_TO_FW_UI. Our menu is the firmware setup screen.
#define BOOT_TO_FW_UI ((UINT64)1)
