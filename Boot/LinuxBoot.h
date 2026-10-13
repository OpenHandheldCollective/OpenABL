/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  LinuxBoot - starts EFI images the way a Boot Loader Specification loader
  does.

  Gzip kernels (Debian and Ubuntu arm64 vmlinuz) are inflated first with the
  ABL's decompress. Initrds are served through a LoadFile2 handle on
  LINUX_EFI_INITRD_MEDIA_GUID, which the arm64 EFI stub uses since Linux 5.8.
  Errors are returned as text in *Err, which the caller frees.
*/

#ifndef __BOOT_LINUX_BOOT_H__
#define __BOOT_LINUX_BOOT_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Core/BootEntry.h"

// Loads Path on Fs as an EFI image, inflating a gzip kernel first. The image
// is loaded from memory but given its file's device path under Dev, so the
// stub knows where it came from.
BOOLEAN
LinuxBootLoad (
  IN EFI_HANDLE   Fs,
  IN OABL_BYTES   Dev,
  IN CONST CHAR8 *Path,
  OUT EFI_HANDLE *Image,
  OUT CHAR8     **Err
);

// Sets Image's load options to Options, unless it is empty. Options holds
// UTF-16 text from LinuxBootTextOptions or a Boot#### option's raw data.
// The image points into Options, so keep it until the image has run.
BOOLEAN
LinuxBootSetOptions (
  IN EFI_HANDLE         Image,
  IN CONST OABL_BUFFER *Options,
  OUT CHAR8           **Err
);

// Converts Text to UTF-16 load options with a terminator. An empty Text
// gives empty options.
BOOLEAN
LinuxBootTextOptions (
  IN CONST CHAR8  *Text,
  OUT OABL_BUFFER *Out
);

// The initrd's LoadFile2 handle. Free it with LinuxBootInitrdFree if the
// kernel returns.
typedef struct _LINUX_BOOT_INITRD LINUX_BOOT_INITRD;

// Concatenates the files in Paths, each padded to 4 bytes as cpio expects,
// and serves them as the initrd. *Out is NULL when there are no paths.
// Fails if another initrd is already registered.
BOOLEAN
LinuxBootInstallInitrd (
  IN EFI_HANDLE                Fs,
  IN CONST BOOT_ENTRY_STRINGS *Paths,
  OUT LINUX_BOOT_INITRD      **Out,
  OUT CHAR8                  **Err
);

// Uninstalls the initrd's interfaces and frees it. Initrd may be NULL.
VOID
LinuxBootInitrdFree (IN LINUX_BOOT_INITRD *Initrd OPTIONAL);

#endif
