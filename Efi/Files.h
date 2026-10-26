/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Files - file access by file system handle and path, device paths, the
  volumes a boot source allows, and raw FAT32 reads.

  Paths are UTF-8, backslash-separated from the root. Files are opened
  read-only unless noted and closed before returning. Results are newly
  allocated and the caller frees them. Running out of memory counts as a
  failed read.
*/

#ifndef __EFI_FILES_H__
#define __EFI_FILES_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"

#include "Core/BootDecision.h"
#include "Core/Settings.h"

// Reads up to Length bytes of Path (UCS-2) from Offset. Returns less if the
// file ends first, FALSE if it can't be opened or read.
BOOLEAN
FilesReadRange (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR16 *Path,
  IN UINT64        Offset,
  IN UINTN         Length,
  OUT OABL_BUFFER *Out
);

// Reads the whole file at Path. Fails if it is larger than Limit bytes.
BOOLEAN
FilesRead (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Path,
  IN UINTN         Limit,
  OUT OABL_BUFFER *Out
);

// Reads a text file as UTF-8, replacing invalid sequences. Returns NULL if
// FilesRead fails.
CHAR8 *
FilesReadText (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path,
  IN UINTN        Limit
);

// FilesReadRange with a UTF-8 path.
BOOLEAN
FilesReadPart (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Path,
  IN UINT64        Offset,
  IN UINTN         Length,
  OUT OABL_BUFFER *Out
);

// Returns TRUE if Path opens and is a file.
BOOLEAN
FilesExists (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path
);

typedef struct _DIR_ENTRY {
  CHAR8  *Name;
  BOOLEAN IsDir;
} FILES_DIR_ENTRY;

typedef struct _DIR_ENTRIES OABL_VEC (FILES_DIR_ENTRY) FILES_DIR_ENTRIES;

// Appends the entries of directory Dir (except "." and "..") to Out.
// Free them with FilesEntriesFree.
VOID
FilesList (
  IN EFI_HANDLE             Fs,
  IN CONST CHAR8           *Dir,
  IN OUT FILES_DIR_ENTRIES *Out
);

VOID
FilesEntriesFree (IN OUT FILES_DIR_ENTRIES *Entries);

// Renames Path to NewName in the same directory with SetInfo, the way
// systemd-boot renames entries for boot counting. Returns
// EFI_INVALID_PARAMETER for a name that doesn't fit UCS-2, EFI_NOT_FOUND if
// the file doesn't open, EFI_BUFFER_TOO_SMALL when out of memory, else the
// firmware's status.
EFI_STATUS
FilesRename (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path,
  IN CONST CHAR8 *NewName
);

// Returns the trimmed volume label, or NULL if it is unset, empty or
// "NO NAME". Caller frees it.
CHAR8 *
FilesVolumeLabel (IN EFI_HANDLE Fs);

// Paths: device paths as raw bytes. Core/EfiData.h has the node helpers.

// Copies the device path of Handle, end node included, into Out. Returns
// FALSE if Handle has no device path.
BOOLEAN
PathsHandlePath (
  IN EFI_HANDLE    Handle,
  OUT OABL_BUFFER *Out
);

// Converts the device path at Path to text for logs, or "?" if it can't.
// Returns NULL only when out of memory.
CHAR8 *
PathsDescribe (
  IN CONST UINT8 *Path,
  IN UINTN        Size
);

// Volumes: the file systems a boot source allows, in the order to try them,
// by the rules in Core/BootDecision.

typedef struct _HANDLE_LIST
OABL_VEC (EFI_HANDLE) VOLUMES_HANDLE_LIST;

// Classifies Handle by its BlockIo's RemovableMedia flag, then by the USB
// and SD nodes in its device path.
BOOT_SOURCE_MEDIUM
VolumesMedium (IN EFI_HANDLE Handle);

typedef struct _VOLUME_FS {
  EFI_HANDLE         Handle;
  BOOT_SOURCE_MEDIUM Medium;
} VOLUME_FS;

typedef struct _VOLUME_FS_LIST OABL_VEC (VOLUME_FS) VOLUME_FS_LIST;

// Appends every file system and its medium to Out, in firmware order.
VOID
VolumesFileSystems (IN OUT VOLUME_FS_LIST *Out);

// Appends the file systems to try for Source to Out, in order.
VOID
VolumesAllowed (
  IN BOOT_SOURCE              Source,
  IN BOOLEAN                  CfwOnInternal,
  IN BOOLEAN                  AltVolumeBoot,
  IN OUT VOLUMES_HANDLE_LIST *Out
);

// RawFat: reads files straight off a FAT32 partition through its BlockIo,
// for when XBL's FAT driver fails a read. Core/Fat32.c does the parsing.

// Reads up to Length bytes of Path from Offset (MAX_UINTN reads to the end)
// off the FAT32 partition on Fs. Fails if it isn't FAT32, the media or file
// is missing, or a read fails.
BOOLEAN
RawFatReadRange (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Path,
  IN UINT64        Offset,
  IN UINTN         Length,
  OUT OABL_BUFFER *Out
);

#endif
