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

// Key has a leading space and a trailing '=', like the ABL's own keys.
typedef struct _KERNEL_CMDLINE_PARAM {
  CONST CHAR8 *Key;
  CONST CHAR8 *Value;
} KERNEL_CMDLINE_PARAM;

// Most parameters KernelCmdLineAndroidParams can return.
#define KERNEL_CMDLINE_ANDROID_PARAMS 1

// Returns the androidboot parameters for XBL's DisplayPanelConfiguration.
// Android on the Pocket S2/FIT needs androidboot.client_id=0x01, and the
// panel identifies that device even in off-mode charging.
UINTN
KernelCmdLineAndroidParams (
  IN CONST CHAR8          *PanelConfig,
  OUT KERNEL_CMDLINE_PARAM Out[KERNEL_CMDLINE_ANDROID_PARAMS]
);

#endif
