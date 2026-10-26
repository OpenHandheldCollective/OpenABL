/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  AndroidBcb - the bootloader message in Android's misc partition.
*/

#ifndef __CORE_ANDROID_BCB_H__
#define __CORE_ANDROID_BCB_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"

// Length of the command field of the bootloader message in misc.
#define ANDROID_BCB_COMMAND_LEN 32

typedef enum {
  ANDROID_BOOT_COMMAND_NONE,
  // boot-recovery
  ANDROID_BOOT_COMMAND_RECOVERY,
  // bootonce-bootloader: stop in the bootloader once.
  ANDROID_BOOT_COMMAND_BOOTLOADER_ONCE,
  ANDROID_BOOT_COMMAND_OTHER,
} ANDROID_BOOT_COMMAND_KIND;

// The command field of struct bootloader_message (first 32 bytes of misc).
typedef struct _ANDROID_BOOT_COMMAND {
  ANDROID_BOOT_COMMAND_KIND Kind;
  // The command text for ANDROID_BOOT_COMMAND_OTHER, otherwise "".
  CHAR8 Other[ANDROID_BCB_COMMAND_LEN + 1];
} ANDROID_BOOT_COMMAND;

ANDROID_BOOT_COMMAND
AndroidParseBcb (
  IN CONST UINT8 *Misc,
  IN UINTN        Size
);

// Fills a misc command field with Command, zero-padded to 32 bytes. Command
// is cut to 31 bytes so the field always ends in a NUL.
VOID
AndroidBcbCommand (
  IN CONST CHAR8 *Command,
  OUT UINT8       Out[ANDROID_BCB_COMMAND_LEN]
);

// Size of AOSP's struct bootloader_message: command[32], status[32],
// recovery[768], stage[32], reserved[1184].
#define ANDROID_BCB_SIZE         2048
#define ANDROID_BCB_RECOVERY_AT  64
#define ANDROID_BCB_RECOVERY_LEN 768

// Fills a bootloader message asking recovery to wipe data:
// "boot-recovery", with "recovery\n--wipe_data\n" as recovery's arguments.
VOID
AndroidBcbWipe (OUT UINT8 Out[ANDROID_BCB_SIZE]);

#endif
