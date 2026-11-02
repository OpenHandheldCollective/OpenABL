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
#include "Efi/Quirks.h"
#include "Efi/Uninstall.h"
#include "Efi/Variables.h"

#define STATUS_TEXT(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

#define FORCE_FILE FW_UPDATE_DIR "\\force"
// abl is 1 MiB on every device.
#define IMAGE_LIMIT  ((UINTN)1 << 20)
#define SHA256_LIMIT 4096
#define WRITE_TRIES  3
#define MAX_STAGED   16

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

// The *.elf files, sorted by name. Caller frees each path.
STATIC UINTN
StagedPaths (
  IN EFI_HANDLE Fs,
  OUT CHAR8    *Paths[MAX_STAGED]
)
{
  FILES_DIR_ENTRIES Entries = {0};
  UINTN             Count   = 0;

  FilesList (Fs, FW_UPDATE_DIR, &Entries);
  for (UINTN Index = 0; Index < Entries.Count && Count < MAX_STAGED; Index++) {
    CONST CHAR8 *Name = Entries.Items[Index].Name;
    CHAR8       *Path;
    UINTN        At;

    if (Entries.Items[Index].IsDir ||
        !TextEndsWithNoCase (TextOf (Name), ".elf")) {
      continue;
    }
    Path = SPrintAlloc ("%s\\%s", FW_UPDATE_DIR, Name);
    if (!Path) {
      continue;
    }
    for (At = Count++; At > 0 && NameBefore (Path, Paths[At - 1]); At--) {
      Paths[At] = Paths[At - 1];
    }
    Paths[At] = Path;
  }
  FilesEntriesFree (&Entries);
  return Count;
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
Remove (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path,
  IN LOG_SINK     Log
)
{
  EFI_STATUS Status = FilesDelete (Fs, Path);

  if (Status != EFI_SUCCESS && Status != EFI_NOT_FOUND) {
    LogPrint (
      Log, "firmware update: %s couldn't be removed: %s", Path,
      STATUS_TEXT (Status)
    );
  }
}

STATIC VOID
RemoveStaged (
  IN EFI_HANDLE Fs,
  IN CHAR8    **Paths,
  IN UINTN      Count,
  IN LOG_SINK   Log
)
{
  for (UINTN Index = 0; Index < Count; Index++) {
    CHAR8 *Sha256 = SPrintAlloc ("%s.sha256", Paths[Index]);

    Remove (Fs, Paths[Index], Log);
    if (Sha256) {
      Remove (Fs, Sha256, Log);
    }
    FreePool (Sha256);
  }
  Remove (Fs, FORCE_FILE, Log);
}

// Reads and checks Path. Image keeps the file.
STATIC VOID
CheckStaged (
  IN EFI_HANDLE             Fs,
  IN CONST CHAR8           *Path,
  IN CONST FW_UPDATE_INPUT *Base,
  OUT OABL_BUFFER          *Image,
  OUT FW_UPDATE_CHECK      *Check
)
{
  CHAR8          *Sha256Path = SPrintAlloc ("%s.sha256", Path);
  CHAR8          *Sha256     = NULL;
  FW_UPDATE_INPUT In         = *Base;

  if (Sha256Path) {
    Sha256 = FilesReadText (Fs, Sha256Path, SHA256_LIMIT);
  }
  if (!FilesRead (Fs, Path, IMAGE_LIMIT, Image)) {
    ZeroMem (Check, sizeof (*Check));
    Check->Status = FW_UPDATE_ERROR_UNSUCCESSFUL;
    AsciiStrCopy (Check->Why, sizeof (Check->Why), "unreadable, or over 1 MiB");
  } else {
    In.Staged = BytesOf (Image->Data, Image->Len);
    In.Name   = Path + sizeof (FW_UPDATE_DIR);
    In.Sha256 = TextOf (Sha256);
    FwUpdateCheck (&In, Check);
  }
  FreePool (Sha256);
  FreePool (Sha256Path);
}

FW_UPDATE_OUTCOME
FwUpdateRun (
  IN CONST MENU_HOST *Host,
  IN EFI_HANDLE      *Volumes,
  IN UINTN            Count,
  IN LOG_SINK         Log
)
{
  EFI_HANDLE        Fs = NULL;
  CHAR8            *Paths[MAX_STAGED];
  UINTN             Staged    = 0;
  OABL_BUFFER       Installed = {0};
  OABL_BUFFER       Image     = {0};
  OABL_BUFFER       Best      = {0};
  ABL_SLOTS         Slots     = {0};
  FW_UPDATE_INPUT   In;
  FW_UPDATE_CHECK   Check;
  FW_UPDATE_CHECK   Pick;
  FW_UPDATE_CHECK   Refused;
  UINT64            PartSize;
  BOOLEAN           HaveRefused = FALSE;
  BOOLEAN           Keep        = FALSE;
  FW_UPDATE_OUTCOME Outcome     = FW_UPDATE_NOT_WRITTEN;

  for (UINTN Index = 0; Index < Count && !Staged; Index++) {
    Fs     = Volumes[Index];
    Staged = StagedPaths (Fs, Paths);
  }
  if (!Staged) {
    return FW_UPDATE_NONE;
  }
  LogPrint (Log, "firmware update: %u image(s) staged", (UINT32)Staged);
  ZeroMem (&Pick, sizeof (Pick));
  ZeroMem (&Refused, sizeof (Refused));
  Pick.Status = Refused.Status = FW_UPDATE_ERROR_UNSUCCESSFUL;
  if (!FindSlots (&Slots, Log)) {
    AsciiStrCopy (Refused.Why, sizeof (Refused.Why), "no abl partition");
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
        Refused.Why, sizeof (Refused.Why), "the installed ABL can't be read"
      );
    } else {
      In = (FW_UPDATE_INPUT){
        .Installed     = BytesOf (Installed.Data, Installed.Len),
        .PartitionSize = PartSize,
        .Chip          = QuirksChipId (),
        .Force         = FilesExists (Fs, FORCE_FILE),
      };
      // Take the newest that passes. If none does, report the first refusal,
      // preferring one for this chip.
      for (UINTN Index = 0; Index < Staged; Index++) {
        CONST CHAR8 *Name = Paths[Index] + sizeof (FW_UPDATE_DIR);

        CheckStaged (Fs, Paths[Index], &In, &Image, &Check);
        if (Check.Status == FW_UPDATE_SUCCESS) {
          LogPrint (Log, "firmware update: %s: %s", Name, Check.Why);
          if (Pick.Status != FW_UPDATE_SUCCESS ||
              Check.Version > Pick.Version) {
            BufferFree (&Best);
            Best  = Image;
            Image = (OABL_BUFFER){0};
            Pick  = Check;
          }
        } else {
          LogPrint (
            Log, "firmware update: %s refused, %s: %s", Name,
            FwUpdateStatusName (Check.Status), Check.Why
          );
          if (!HaveRefused || (Refused.OtherDevice && !Check.OtherDevice)) {
            Refused     = Check;
            HaveRefused = TRUE;
          }
        }
        BufferFree (&Image);
      }
      if (Pick.Status != FW_UPDATE_SUCCESS && Refused.OtherDevice &&
          Staged > 1) {
        SPrint (
          Refused.Why, sizeof (Refused.Why),
          "none of the %u images is for this chip", (UINT32)Staged
        );
      }
    }
  }
  if (Pick.Status != FW_UPDATE_SUCCESS) {
    LogPrint (
      Log, "firmware update refused, %s: %s",
      FwUpdateStatusName (Refused.Status), Refused.Why
    );
    Record (Refused.Version, Refused.Status, Log);
  } else if (Pick.AlreadyInstalled) {
    LogPrint (Log, "firmware update: %s; nothing to write", Pick.Why);
  } else if (MenuHostAction (Host, OPENABL_MENU_ACTION_BATTERY_OK) !=
             EFI_SUCCESS) {
    LogLine (
      Log, "firmware update: the battery is too low; staged for the next boot"
    );
    Record (Pick.Version, FW_UPDATE_ERROR_BATTERY, Log);
    Keep = TRUE;
  } else {
    LogPrint (Log, "firmware update: installing %s; don't power off", Pick.Why);
    if (Install (
          &Slots, BytesOf (Best.Data, Best.Len),
          BytesOf (Installed.Data, Pick.Old.Size), Log
        )) {
      Outcome = FW_UPDATE_WRITTEN;
    }
    Record (
      Pick.Version,
      Outcome == FW_UPDATE_WRITTEN ? FW_UPDATE_SUCCESS
                                   : FW_UPDATE_ERROR_UNSUCCESSFUL,
      Log
    );
  }
  if (!Keep) {
    RemoveStaged (Fs, Paths, Staged, Log);
  }
  for (UINTN Index = 0; Index < Staged; Index++) {
    FreePool (Paths[Index]);
  }
  BufferFree (&Best);
  BufferFree (&Installed);
  return Outcome;
}
