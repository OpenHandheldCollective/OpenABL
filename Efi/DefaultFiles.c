/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DefaultFiles - boots the removable-media default file.
*/

#include "Efi/DefaultFiles.h"

#include "Base/OablBase.h"

#include "Base/Memory.h"
#include "Base/Buffer.h"
#include "Base/Print.h"
#include "Core/EfiData.h"
#include "Efi/Files.h"
#include "Efi/Efi.h"

CONST CHAR8 *CONST gDefaultFilesPaths[2] = {"\\EFI\\BOOT\\BOOTAA64.EFI",
                                            "\\EFI\\BOOT\\BOOTARM.EFI"};

#define PATHS (sizeof (gDefaultFilesPaths) / sizeof (*gDefaultFilesPaths))

// Logs Line, which may be NULL, and frees it.
STATIC VOID
LogOwned (
  IN CONST DEFAULT_FILES_HOST *Host,
  IN CHAR8                    *Line OPTIONAL
)
{
  if (Line) {
    Host->Log (Host->Ctx, Line);
  }
  FreePool (Line);
}

#define SAY(Host, ...)                                                         \
  LogOwned ((Host), SPrintAlloc ("default file: " __VA_ARGS__))

#define STATUS_STR(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

STATIC BOOLEAN
ReadRaw (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Path,
  OUT OABL_BUFFER *Out
)
{
  return RawFatReadRange (Fs, Path, 0, MAX_UINTN, Out);
}

// Loads the image from DevicePath, or from Buffer if given. BootPolicy is
// FALSE, so the path must match the file.
STATIC EFI_STATUS
Load (
  IN CONST UINT8       *DevicePath,
  IN CONST OABL_BUFFER *Buffer OPTIONAL,
  OUT EFI_HANDLE       *Image
)
{
  EFI_DEVICE_PATH_PROTOCOL *Path = (EFI_DEVICE_PATH_PROTOCOL *)DevicePath;

  *Image = NULL;
  if (Buffer) {
    return gBS->LoadImage (
      FALSE, gImageHandle, Path, Buffer->Data, Buffer->Len, Image
    );
  }
  return gBS->LoadImage (FALSE, gImageHandle, Path, NULL, 0, Image);
}

DEFAULT_FILES_OUTCOME
DefaultFilesBoot (
  IN CONST EFI_HANDLE         *Drives,
  IN UINTN                     Count,
  IN CONST DEFAULT_FILES_HOST *Host,
  OUT EFI_STATUS              *Returned OPTIONAL
)
{
  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_HANDLE   Fs = Drives[Index];
    EFI_HANDLE   Image;
    OABL_BUFFER  Buffer     = {0};
    OABL_BUFFER  DrivePath  = {0};
    OABL_BUFFER  FileNode   = {0};
    OABL_BUFFER  FullPath   = {0};
    BOOLEAN      HaveBuffer = FALSE;
    CONST CHAR8 *Path       = NULL;
    EFI_STATUS   Status;

    // Look through the file system first, then read FAT32 directly.
    for (UINTN Index2 = 0; Index2 < PATHS && !Path; Index2++) {
      if (FilesExists (Fs, gDefaultFilesPaths[Index2])) {
        Path = gDefaultFilesPaths[Index2];
      }
    }
    if (!Path) {
      for (UINTN Index2 = 0; Index2 < PATHS && !Path; Index2++) {
        if (ReadRaw (Fs, gDefaultFilesPaths[Index2], &Buffer)) {
          Path       = gDefaultFilesPaths[Index2];
          HaveBuffer = TRUE;
        }
      }
      if (!Path) {
        continue;
      }
      SAY (Host, "%s readable only straight off FAT32", Path);
    }
    if (!PathsHandlePath (Fs, &DrivePath)) {
      goto Next;
    }
    if (!UefiFilePathNode (Path, &FileNode)) {
      goto Next;
    }
    if (!UefiJoin (
          (OABL_BYTES[]){BufferBytes (&DrivePath), BufferBytes (&FileNode)}, 2,
          &FullPath
        )) {
      goto Next;
    }
    SAY (Host, "loading %s", Path);
    Status = Load (FullPath.Data, HaveBuffer ? &Buffer : NULL, &Image);
    if (Status != EFI_SUCCESS) {
      // XBL's FAT driver, or the earlier raw copy, failed. Read it again
      // straight off FAT32.
      SAY (
        Host, "LoadImage failed (%s): trying straight off FAT32",
        STATUS_STR (Status)
      );
      BufferFree (&Buffer);
      HaveBuffer = ReadRaw (Fs, Path, &Buffer);
      if (!HaveBuffer || Load (FullPath.Data, &Buffer, &Image) != EFI_SUCCESS) {
        SAY (Host, "no luck straight off FAT32 either");
        goto Next;
      }
    }
    BufferFree (&Buffer);
    Host->BeforeStart (Host->Ctx, Fs);
    Status = StartImageFreeExitData (Image);
    Host->AfterReturn (Host->Ctx);
    if (EFI_ERROR (Status)) {
      (VOID)gBS->UnloadImage (Image);
    }
    SAY (Host, "%s returned: %s", Path, STATUS_STR (Status));
    BufferFree (&DrivePath);
    BufferFree (&FileNode);
    BufferFree (&FullPath);
    if (Returned) {
      *Returned = Status;
    }
    return DEFAULT_FILES_OUTCOME_RETURNED;
  Next:
    BufferFree (&Buffer);
    BufferFree (&DrivePath);
    BufferFree (&FileNode);
    BufferFree (&FullPath);
  }
  return DEFAULT_FILES_OUTCOME_NOT_FOUND;
}
