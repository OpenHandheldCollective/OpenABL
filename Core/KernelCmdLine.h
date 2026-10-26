/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  KernelCmdLine - Kernel command line changes made by the ABL.

  Some devices need extra androidboot parameters. The ABL adds them in
  UpdateCmdLine.c.
*/

#ifndef __CORE_KERNEL_CMD_LINE_H__
#define __CORE_KERNEL_CMD_LINE_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"

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
