/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  KernelCmdLine - Kernel command line changes made by the ABL.

  cmdline.txt replaces a Linux image's command line; cmdline-append.txt is
  added after it. Some devices need extra androidboot parameters. The ABL
  applies the result in UpdateCmdLine.c.
*/

#ifndef __CORE_KERNEL_CMD_LINE_H__
#define __CORE_KERNEL_CMD_LINE_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"

// Replaces the image's command line.
#define KERNEL_CMDLINE_OVERRIDE "cmdline.txt"
// Added after the image's command line.
#define KERNEL_CMDLINE_APPEND "cmdline-append.txt"

typedef enum {
  KERNEL_CMDLINE_KIND_OVERRIDE,
  KERNEL_CMDLINE_KIND_APPEND,
} KERNEL_CMDLINE_KIND;

// Copies a file's text into the empty Out. Line ends become spaces,
// trailing spaces are dropped, and the text stops at the first NUL.
BOOLEAN
KernelCmdLineFileText (
  IN CONST UINT8     *Raw,
  IN UINTN            Size,
  IN OUT OABL_BUFFER *Out
);

// Builds the command line into the empty Out. An override is Text alone;
// an append is Base, a space if needed, then Text.
BOOLEAN
KernelCmdLineCombine (
  IN OABL_BYTES          Base,
  IN KERNEL_CMDLINE_KIND Kind,
  IN OABL_BYTES          Text,
  IN OUT OABL_BUFFER    *Out
);

// Names the partition a boot image was loaded from, as PARTUUID=<guid>, so
// the initramfs mounts that installation. An SD card written from an image
// and an internal install made from it carry the same labels.
#define KERNEL_CMDLINE_BOOT_PART "portare.bootpart"

// Appends KERNEL_CMDLINE_BOOT_PART "=PARTUUID=" PartUuid to the
// unterminated Out, after a space unless Out is empty or ends in one.
BOOLEAN
KernelCmdLineAppendBootPart (
  IN OUT OABL_BUFFER *Out,
  IN CONST CHAR8     *PartUuid
);

// Key has a leading space and a trailing '=', like the ABL's own keys.
typedef struct _KERNEL_CMDLINE_PARAM {
  CONST CHAR8 *Key;
  CHAR8        Value[8];
} KERNEL_CMDLINE_PARAM;

// Most parameters KernelCmdLineAndroidParams can return.
#define KERNEL_CMDLINE_ANDROID_PARAMS 4

// The ids AYANEO/KONKR's stock ABLs pass Android, for their panels only:
UINTN
KernelCmdLineAndroidParams (
  IN CONST CHAR8          *PanelConfig,
  IN UINT32                Subtype,
  IN UINT8                 DisplayId,
  IN BOOLEAN               WithDisplayId,
  OUT KERNEL_CMDLINE_PARAM Out[KERNEL_CMDLINE_ANDROID_PARAMS]
);

#endif
