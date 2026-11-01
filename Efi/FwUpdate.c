/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  FwUpdate - finds, checks and writes a staged ABL image.
*/

#include "Efi/FwUpdate.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Core/AbSlot.h"
#include "Core/FwUpdate.h"
#include "Core/Settings.h"
#include "Efi/Files.h"
#include "Efi/Uninstall.h"
#include "Efi/Variables.h"

#define STATUS_TEXT(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

#define FORCE_FILE FW_UPDATE_DIR "\\force"
// abl is 1 MiB on every device.
#define IMAGE_LIMIT  ((UINTN)1 << 20)
#define SHA256_LIMIT 4096
#define WRITE_TRIES  3

// Active slot first. Letter is 0 on devices without slots.
typedef struct _ABL_SLOTS {
  UINTN               Count;
  UNINSTALL_PARTITION Part[2];
  CHAR8               Letter[2];
} ABL_SLOTS;

STATIC BOOLEAN
NameBefore (
  IN CONST CHAR8 *A,
  IN CONST CHAR8 *B
)
{
  while (*A && *A == *B) {
    A++;
    B++;
  }
  return (UINT8)*A < (UINT8)*B;
}

// First *.elf by name. Caller frees it.
STATIC CHAR8 *
StagedPath (IN EFI_HANDLE Fs)
{
  FILES_DIR_ENTRIES Entries = {0};
  CONST CHAR8      *First   = NULL;
  CHAR8            *Path    = NULL;

  FilesList (Fs, FW_UPDATE_DIR, &Entries);
  for (UINTN Index = 0; Index < Entries.Count; Index++) {
    CONST CHAR8 *Name = Entries.Items[Index].Name;

    if (!Entries.Items[Index].IsDir &&
        TextEndsWithNoCase (TextOf (Name), ".elf") &&
        (!First || NameBefore (Name, First))) {
      First = Name;
    }
  }
  if (First) {
    Path = SPrintAlloc ("%s\\%s", FW_UPDATE_DIR, First);
  }
  FilesEntriesFree (&Entries);
  return Path;
}

// The active slot comes from boot_a/boot_b, the same way the ABL picks it.
STATIC BOOLEAN
FindSlots (
  OUT ABL_SLOTS *Out,
  IN LOG_SINK    Log
)
{
  UNINSTALL_PARTITION A;
  UNINSTALL_PARTITION B;
  UNINSTALL_PARTITION Boot;
  AB_PARTITION        Boots[2];
  UINTN               Known = 0;
  CHAR8               Active;
  AB_REASON           Reason;

  ZeroMem (Out, sizeof (*Out));
  if (UninstallFindPartition ("abl_a", &A) == 1 &&
      UninstallFindPartition ("abl_b", &B) == 1) {
    if (UninstallFindPartition ("boot_a", &Boot) == 1) {
      Boots[Known++] = (AB_PARTITION){"boot_a", Boot.Attributes};
    }
    if (UninstallFindPartition ("boot_b", &Boot) == 1) {
      Boots[Known++] = (AB_PARTITION){"boot_b", Boot.Attributes};
    }
    if (!AbActiveSlot (Boots, Known, &Active, &Reason)) {
      LogLine (Log, "firmware update: no active slot");
      return FALSE;
    }
    Out->Count     = 2;
    Out->Part[0]   = Active == 'b' ? B : A;
    Out->Part[1]   = Active == 'b' ? A : B;
    Out->Letter[0] = Active == 'b' ? 'b' : 'a';
    Out->Letter[1] = Active == 'b' ? 'a' : 'b';
    LogPrint (
      Log, "firmware update: slot %c is active (%s)", Out->Letter[0],
      AbReasonName (Reason)
    );
    return TRUE;
  }
  if (UninstallFindPartition ("abl", &A) == 1) {
    Out->Count   = 1;
    Out->Part[0] = A;
    return TRUE;
  }
  LogLine (Log, "firmware update: no abl_a and abl_b, and no single abl");
  return FALSE;
}

STATIC UINT64
PartBytes (IN CONST UNINSTALL_PARTITION *Part)
{
  return Part->Last < Part->First
           ? 0
           : (Part->Last - Part->First + 1) * Part->BlockIo->Media->BlockSize;
}

// Write, flush and read back. EFI_CRC_ERROR if it reads back different.
STATIC EFI_STATUS
WriteChecked (
  IN CONST UNINSTALL_PARTITION *Part,
  IN CONST UINT8               *Image,
  IN UINTN                      Size
)
{
  UINTN       BlockSize = Part->BlockIo->Media->BlockSize;
  UINTN       Length    = (Size + BlockSize - 1) / BlockSize * BlockSize;
  UINT8      *Padded    = AllocateZeroPool (Length);
  OABL_BUFFER Back      = {0};
  EFI_STATUS  Status;

  if (!Padded) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem (Padded, Image, Size);
  Status = UninstallWrite (Part->BlockIo, Part->First, Padded, Length);
  if (Status == EFI_SUCCESS) {
    Status = Part->BlockIo->FlushBlocks (Part->BlockIo);
  }
  if (Status == EFI_SUCCESS) {
    if (!UninstallRead (Part->BlockIo, Part->First, Length, &Back)) {
      Status = EFI_DEVICE_ERROR;
    } else if (CompareMem (Back.Data, Padded, Length) != 0) {
      Status = EFI_CRC_ERROR;
    }
  }
  BufferFree (&Back);
  FreePool (Padded);
  return Status;
}

// Inactive slot first. A slot that keeps failing gets the old image back.
STATIC BOOLEAN
Install (
  IN CONST ABL_SLOTS *Slots,
  IN OABL_BYTES       Image,
  IN OABL_BYTES       Installed,
  IN LOG_SINK         Log
)
{
  for (UINTN Slot = Slots->Count; Slot-- > 0;) {
    CONST UNINSTALL_PARTITION *Part    = &Slots->Part[Slot];
    EFI_STATUS                 Status  = EFI_NOT_STARTED;
    CHAR8                      Name[8] = "abl";

    if (Slots->Letter[Slot]) {
      SPrint (Name, sizeof (Name), "abl_%c", Slots->Letter[Slot]);
    }
    for (UINTN Try = 0; Try < WRITE_TRIES && Status != EFI_SUCCESS; Try++) {
      Status = WriteChecked (Part, Image.Ptr, Image.Len);
      LogPrint (
        Log, "firmware update: %s%s written and read back: %s", Name,
        Slot == 0 && Slots->Count > 1 ? " (active)" : "", STATUS_TEXT (Status)
      );
    }
    if (Status != EFI_SUCCESS) {
      Status = WriteChecked (Part, Installed.Ptr, Installed.Len);
      LogPrint (
        Log, "firmware update: %s restored to the installed image: %s", Name,
        STATUS_TEXT (Status)
      );
      return FALSE;
    }
  }
  return TRUE;
}

// For the ESRT.
STATIC VOID
Record (
  IN UINT32   Version,
  IN UINT32   Status,
  IN LOG_SINK Log
)
{
  UINT8 Data[FW_UPDATE_RECORD_SIZE];

  WriteLe32 (Data, Version);
  WriteLe32 (Data + 4, Status);
  if (VariablesPut (SETTINGS_KEY_FIRMWARE_UPDATE, Data, sizeof (Data)) !=
        EFI_SUCCESS ||
      VariablesFlush () != EFI_SUCCESS) {
    LogLine (Log, "firmware update: the result couldn't be stored in misc");
  }
}

STATIC VOID
RemoveStaged (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path,
  IN LOG_SINK     Log
)
{
  CHAR8       *Sha256   = SPrintAlloc ("%s.sha256", Path);
  CONST CHAR8 *Paths[3] = {Path, Sha256, FORCE_FILE};

  for (UINTN Index = 0; Index < 3; Index++) {
    EFI_STATUS Status;

    if (!Paths[Index]) {
      continue;
    }
    Status = FilesDelete (Fs, Paths[Index]);
    if (Status != EFI_SUCCESS && Status != EFI_NOT_FOUND) {
      LogPrint (
        Log, "firmware update: %s couldn't be removed: %s", Paths[Index],
        STATUS_TEXT (Status)
      );
    }
  }
  FreePool (Sha256);
}

FW_UPDATE_OUTCOME
FwUpdateRun (
  IN CONST MENU_HOST *Host,
  IN EFI_HANDLE      *Volumes,
  IN UINTN            Count,
  IN LOG_SINK         Log
)
{
  EFI_HANDLE        Fs     = NULL;
  CHAR8            *Path   = NULL;
  CHAR8            *Sha256 = NULL;
  CHAR8            *Sha256Path;
  OABL_BUFFER       Staged    = {0};
  OABL_BUFFER       Installed = {0};
  ABL_SLOTS         Slots     = {0};
  FW_UPDATE_INPUT   In;
  FW_UPDATE_CHECK   Check;
  UINT64            PartSize;
  BOOLEAN           Keep    = FALSE;
  FW_UPDATE_OUTCOME Outcome = FW_UPDATE_NOT_WRITTEN;

  for (UINTN Index = 0; Index < Count && !Path; Index++) {
    Fs   = Volumes[Index];
    Path = StagedPath (Fs);
  }
  if (!Path) {
    return FW_UPDATE_NONE;
  }
  LogPrint (Log, "firmware update: %s is staged", Path);
  Sha256Path = SPrintAlloc ("%s.sha256", Path);
  if (Sha256Path) {
    Sha256 = FilesReadText (Fs, Sha256Path, SHA256_LIMIT);
    FreePool (Sha256Path);
  }
  ZeroMem (&Check, sizeof (Check));
  Check.Status = FW_UPDATE_ERROR_UNSUCCESSFUL;
  if (!FilesRead (Fs, Path, IMAGE_LIMIT, &Staged)) {
    AsciiStrCopy (Check.Why, sizeof (Check.Why), "unreadable, or over 1 MiB");
  } else if (!FindSlots (&Slots, Log)) {
    AsciiStrCopy (Check.Why, sizeof (Check.Why), "no abl partition");
  } else {
    PartSize = PartBytes (&Slots.Part[0]);
    if (Slots.Count > 1 && PartBytes (&Slots.Part[1]) < PartSize) {
      PartSize = PartBytes (&Slots.Part[1]);
    }
    if (!UninstallRead (
          Slots.Part[0].BlockIo, Slots.Part[0].First,
          (UINTN)(PartSize < IMAGE_LIMIT ? PartSize : IMAGE_LIMIT), &Installed
        )) {
      AsciiStrCopy (
        Check.Why, sizeof (Check.Why), "the installed ABL can't be read"
      );
    } else {
      In = (FW_UPDATE_INPUT){
        .Staged        = BytesOf (Staged.Data, Staged.Len),
        .Name          = Path + sizeof (FW_UPDATE_DIR),
        .Sha256        = TextOf (Sha256),
        .Installed     = BytesOf (Installed.Data, Installed.Len),
        .PartitionSize = PartSize,
        .Force         = FilesExists (Fs, FORCE_FILE),
      };
      FwUpdateCheck (&In, &Check);
    }
  }
  if (Check.AlreadyInstalled) {
    LogPrint (Log, "firmware update: %s; nothing to write", Check.Why);
  } else if (Check.Status != FW_UPDATE_SUCCESS) {
    LogPrint (
      Log, "firmware update refused, %s: %s", FwUpdateStatusName (Check.Status),
      Check.Why
    );
    Record (Check.Version, Check.Status, Log);
  } else if (MenuHostAction (Host, OPENABL_MENU_ACTION_BATTERY_OK) !=
             EFI_SUCCESS) {
    LogLine (
      Log, "firmware update: the battery is too low; staged for the next boot"
    );
    Record (Check.Version, FW_UPDATE_ERROR_BATTERY, Log);
    Keep = TRUE;
  } else {
    LogPrint (
      Log, "firmware update: installing %s; don't power off", Check.Why
    );
    if (Install (
          &Slots, BytesOf (Staged.Data, Staged.Len),
          BytesOf (Installed.Data, Check.Old.Size), Log
        )) {
      Outcome = FW_UPDATE_WRITTEN;
    }
    Record (
      Check.Version,
      Outcome == FW_UPDATE_WRITTEN ? FW_UPDATE_SUCCESS
                                   : FW_UPDATE_ERROR_UNSUCCESSFUL,
      Log
    );
  }
  if (!Keep) {
    RemoveStaged (Fs, Path, Log);
  }
  BufferFree (&Staged);
  BufferFree (&Installed);
  FreePool (Sha256);
  FreePool (Path);
  return Outcome;
}
