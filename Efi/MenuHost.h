/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  MenuHost - the protocol the ABL offers the menu it embeds
  (Protocol/OpenAblMenuHost.h in the ABL).

  The ABL installs it on its image handle and starts the menu from its
  firmware volume. The menu draws, reads keys and keeps the settings. The ABL
  reports its state, restarts and powers off, and serves fastboot on each
  Poll.
*/

#ifndef __EFI_MENU_HOST_H__
#define __EFI_MENU_HOST_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Protocol/OpenAblMenuHost.h>

#include "Core/BootDecision.h"

// Why the last Start Linux came back. Kept across a restart, so the values
// stay.
#define MENU_START_RESULT_NONE 0u
// Nothing to boot.
#define MENU_START_RESULT_NOTHING 1u
// An image ran and returned, with no boot image after it.
#define MENU_START_RESULT_RETURNED 2u
// The boot image needs a device model.
#define MENU_START_RESULT_NO_MODEL 3u
// The boot image didn't verify or didn't start.
#define MENU_START_RESULT_FAILED 4u
// USB host didn't come up (no drive, or XBL kept the core).
#define MENU_START_RESULT_NO_USB 5u
// What the boot list picked is on a drive that's gone (the SD card taken
// out), so nothing was started.
#define MENU_START_RESULT_PICK_GONE 6u
// What the boot list picked didn't start, so nothing else was tried.
#define MENU_START_RESULT_PICK_FAILED 7u
// Android's start came back.
#define MENU_START_RESULT_ANDROID_FAILED 0x80u

// devinfo boot source codes (the ABL's BOOT_SOURCE_*).
#define MENU_HOST_BOOT_SOURCE_INTERNAL 0u
#define MENU_HOST_BOOT_SOURCE_AUTO     1u
#define MENU_HOST_BOOT_SOURCE_USB      2u
#define MENU_HOST_BOOT_SOURCE_SDCARD   3u

// Returns an OPENABL_MENU_ACTION_* name for the log, or "?".
CONST CHAR8 *
MenuHostActionName (IN UINT32 Action);

// The ABL's state, and the settings the menu decides from it.
typedef struct _MENU_HOST_STATE {
  UINT8  OffmodeCharging; // Qualcomm's charger screen flag
  UINT8  Flags;           // OPENABL_MENU_STATE_*
  UINT8  Entry;           // OPENABL_MENU_ENTRY_*
  UINT16 Key;             // the key held at the start, a SCAN_* code
  CHAR8  Version[32];     // the ABL's version
  // 1 boots Android.
  UINT8 BootMode;
  // 1 verbose.
  UINT8 AblMode;
  UINT8 AltVolumeUp;
  // MENU_HOST_BOOT_SOURCE_*
  UINT8 BootSource;
  // Alternative Volume Up held: any external drive boots.
  BOOLEAN AltVolumeBoot;
} MENU_HOST_STATE;

// The device powered on for off-mode charging, so Android boots for its
// charger screen. Set by the menu; read by MenuHostState.
extern BOOLEAN gMenuHostChargerBoot;

// A located host. The host keeps the protocol installed while the menu runs,
// so a saved Protocol pointer P can be used again as (MENU_HOST){P}.
typedef struct _MENU_HOST {
  OPENABL_MENU_HOST_PROTOCOL *Protocol;
} MENU_HOST;

// Finds the protocol on the first handle that has it. FALSE if the menu
// wasn't started by the ABL.
BOOLEAN
MenuHostLocate (OUT MENU_HOST *Out);

// Returns the protocol's OPENABL_MENU_FLAG_* bits.
STATIC inline UINT32
MenuHostFlags (IN CONST MENU_HOST *Host)
{
  return Host->Protocol->Flags;
}

// Reads the ABL's state and decides the settings from it.
EFI_STATUS
MenuHostState (
  IN CONST MENU_HOST  *Host,
  OUT MENU_HOST_STATE *Out
);

// Calls DoAction with an OPENABL_MENU_ACTION_* code.
EFI_STATUS
MenuHostAction (
  IN CONST MENU_HOST *Host,
  IN UINT32           Action
);

// Serves fastboot (Poll). Anything but EFI_SUCCESS means leave the menu.
EFI_STATUS
MenuHostPoll (IN CONST MENU_HOST *Host);

// Sends a line to the ABL's debug log, then to AblConsoleProgramLine.
VOID
MenuHostLog (
  IN CONST MENU_HOST *Host,
  IN CONST CHAR8     *Line
);

// A OPENABL_STORAGE_LOG that logs through the host, for Efi/Quirks. Locates
// the host on each call; invalid UTF-8 is replaced.
VOID EFIAPI
MenuHostLogFn (IN CONST CHAR8 *Line OPTIONAL);

// Updates the devicetree and starts the kernel with Qualcomm's code. Returns
// only if the kernel didn't start.
EFI_STATUS
MenuHostStartKernel (
  IN CONST MENU_HOST      *Host,
  IN CONST OPENABL_KERNEL *Kernel
);

// Hands the host a boot image in pool memory; the host owns it from then on.
EFI_STATUS
MenuHostSetBootImage (
  IN CONST MENU_HOST *Host,
  IN VOID            *Buffer,
  IN UINTN            Size
);

// Returns a NUL-padded state field of Size bytes as UTF-8, up to its first
// NUL, with invalid bytes replaced. Caller frees it; NULL only when out of
// memory.
CHAR8 *
MenuHostText (
  IN CONST CHAR8 *Field,
  IN UINTN        Size
);

// Returns what the ABL read at start: the key and Entry's fastboot and
// recovery bits.
BOOT_DECISION_START
MenuHostStateStart (IN CONST MENU_HOST_STATE *State);

#endif
