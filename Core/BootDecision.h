/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootDecision - the boot mode chosen from the state read at ABL start, and
  the drives each boot source allows.

  The inputs are the key held (as Qualcomm's GetKeyPress reads it),
  Qualcomm's fastboot and recovery flags, the menu's settings and the
  charger check.
*/

#ifndef __CORE_BOOT_DECISION_H__
#define __CORE_BOOT_DECISION_H__

#include "Base/OablBase.h"

#include "Core/Settings.h"

// UEFI SCAN_* codes.
#define BOOT_DECISION_SCAN_UP     0x01
#define BOOT_DECISION_SCAN_DOWN   0x02
#define BOOT_DECISION_SCAN_HOME   0x05
#define BOOT_DECISION_SCAN_DELETE 0x08

typedef struct _BOOT_DECISION_START {
  // SCAN_* code of the key held, or 0.
  UINT16 Key;
  // Set by Volume Down or the reboot reason.
  BOOLEAN Fastboot;
  // Set by Volume Up, the reboot reason or the misc partition.
  BOOLEAN Recovery;
} BOOT_DECISION_START;

// Returns TRUE for a fastboot boot: Qualcomm's flag, or Delete held.
BOOLEAN
BootDecisionFastboot (IN CONST BOOT_DECISION_START *Start);

// Returns TRUE if Volume Up or Home is held on a boot that isn't fastboot.
BOOLEAN
BootDecisionUpHeld (IN CONST BOOT_DECISION_START *Start);

// Returns TRUE for the Android boot mode: set in the settings, Volume Up or
// Home held, or a charger boot.
BOOLEAN
BootDecisionAndroid (
  IN CONST BOOT_DECISION_START *Start,
  IN BOOLEAN                    AndroidSetting,
  IN BOOLEAN                    Charger
);

BOOLEAN
BootDecisionAltBoot (
  IN CONST BOOT_DECISION_START *Start,
  IN BOOLEAN                    AltSetting
);

// Returns TRUE to boot Android's recovery instead of its normal boot. In the
// Android boot mode, Qualcomm's recovery flag or Home held selects it. In
// the Linux boot mode, Volume Up boots Android itself, and since Qualcomm's
// flag can't tell Volume Up from its other sources, Volume Up wins.
BOOLEAN
BootDecisionRecovery (
  IN CONST BOOT_DECISION_START *Start,
  IN BOOLEAN                    AndroidSetting
);

// Boot sources: which drives the loader may look at, and in what order.

typedef struct _BOOT_SOURCE_MEDIUM {
  // Removable media, or on a USB or SD path.
  BOOLEAN External;
  BOOLEAN Usb;
} BOOT_SOURCE_MEDIUM;

// Starts a drive's medium from BlockIo's RemovableMedia. Then pass each node
// of the drive's device path to BootSourceMediumNode.
BOOT_SOURCE_MEDIUM
BootSourceMediumOf (IN BOOLEAN Removable);

VOID
BootSourceMediumNode (
  IN OUT BOOT_SOURCE_MEDIUM *Medium,
  IN UINT8                   Type,
  IN UINT8                   SubType
);

BOOLEAN
BootSourceMediumInternal (IN BOOT_SOURCE_MEDIUM Medium);

// Internal only applies once Linux is installed internally. Otherwise it
// means the SD card.
BOOT_SOURCE
BootSourceEffective (
  IN BOOT_SOURCE Source,
  IN BOOLEAN     CfwOnInternal
);

// Returns TRUE if a drive may be booted from or scanned. AltVolumeBoot means
// the alternative Volume Up boot was held, which allows any external drive.
BOOLEAN
BootSourceAllows (
  IN BOOT_SOURCE        Source,
  IN BOOLEAN            AltVolumeBoot,
  IN BOOT_SOURCE_MEDIUM Medium
);

// Fills Order (room for Count) with the indices of the drives that
// BootSourceAllows accepts and returns how many. Source is the stored
// setting. In Auto, without the Volume Up override, internal drives go first.
UINTN
BootSourceSelect (
  IN BOOT_SOURCE               Source,
  IN BOOLEAN                   CfwOnInternal,
  IN BOOLEAN                   AltVolumeBoot,
  IN CONST BOOT_SOURCE_MEDIUM *Media,
  IN UINTN                     Count,
  OUT UINTN                   *Order
);

#endif
