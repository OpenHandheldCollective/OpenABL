/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Quirks - live patches for bugs in XBL's drivers, the storage protocol the
  early copy of the menu leaves installed for the ABL, and an override that
  makes XBL's security checks accept every image.
*/

#ifndef __EFI_QUIRKS_H__
#define __EFI_QUIRKS_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Protocol/OpenAblMenuHost.h>

#include "Efi/Serial.h"

// XBL's SdccDxe asks every SD card for 1.8 V signalling (S18R in ACMD41)
// but never sends CMD11, so UHS cards that wait for it are never found.
// Clears S18R at every ACMD41 site in firmware code outside this image (at
// most 16), and on SM8650 and SM8750 sets XPC there too. With S18R clear, 
// the builds that would then ask a card for the slot's UHS speed ask for 
// high speed instead. Every build gives a card one second to power up; that 
// becomes four seconds. Then signals card detection again, and puts the second back.
// The OABL_SDCC_* builds patch more SdccDxe words, and the G2 Gen 2 also gets
// its SD command line pull-up. With OABL_SD_DIAGNOSTICS the SD slot is logged
// before and after. Returns the number of S18R sites patched.
UINT32
QuirksSdccUhs (
  IN UINT32   Chip,
  IN LOG_SINK Log
);

// Signals XBL's SD card detection event group
// (b7972c36-8a4c-4a56-8b02-1159b52d4bfb).
VOID
QuirksSignalSdDetect (VOID);

// The SM8250's PartitionDxe "repairs" a card's GPT, and overwrites an MBR
// card's MBR, when it binds it. Retargets two branches so it declines
// instead, in every loaded image that has them (XBL's own image holds
// dormant copies). SM8250 (id 356) only, once a boot. Call it right before
// the first connect: patching or even scanning at the ABL's start
// bootloops.
VOID
QuirksGptClobber (
  IN UINT32   Chip,
  IN LOG_SINK Log
);

// The SM8250's FAT driver gives a volume more FAT-cache groups than its tag
// array holds, overflowing into the data cache. Caps each mounted volume's
// FAT-cache group mask to the data cache's, when the layout is the known
// one. SM8250 only.
VOID
QuirksFatCache (
  IN UINT32   Chip,
  IN LOG_SINK Log
);

// Connects every controller, with the GPT quirk before, and an image in
// userdata and the FAT quirk after.
VOID
QuirksConnectAll (
  IN UINT32   Chip,
  IN LOG_SINK Log
);

// Gets storage ready to read: signals SD card detection, then
// QuirksConnectAll.
VOID
QuirksPrepareStorage (
  IN UINT32   Chip,
  IN LOG_SINK Log
);

// Returns ChipInfo's chip id, or 0 if it can't be read.
UINT32
QuirksChipId (VOID);

// Applies the devicetree fix-ups for a kernel. Fdt may grow up to Cap
// bytes. After a USB boot the USB controller is put in host mode. XBL's
// splash memory is reserved unless a display-off is asked for.
VOID
QuirksFixupDeviceTree (
  IN OUT UINT8 *Fdt,
  IN UINTN      Cap,
  IN BOOLEAN    Usb,
  IN LOG_SINK   Log
);

// The settings Qualcomm's code in the ABL reads, one byte each, 0 when
// unset.
typedef struct _QUIRKS_ABL_SETTINGS {
  // 0 Linux, 1 Android.
  UINT8 BootMode;
  // 0 silent, 1 verbose.
  UINT8 AblMode;
  UINT8 AltVolumeUp;
  // MENU_HOST_BOOT_SOURCE_* (devinfo's numbering).
  UINT8 BootSource;
  // 0, 1 or QUIRKS_SETTING_UNSET.
  UINT8 OffmodeCharging;
} QUIRKS_ABL_SETTINGS;

// OffmodeCharging when the setting isn't set; devinfo's value applies.
#define QUIRKS_SETTING_UNSET 0xFFu

// Sets the entry point Run calls. Call before QuirksInstallProtocol.
VOID
QuirksSetRun (IN OPENABL_STORAGE_RUN Run);

// Writes "openabl-menu: " and Line to the ABL's logger. Does nothing if Log is
// NULL.
VOID
QuirksToAbl (
  IN OPENABL_STORAGE_LOG Log OPTIONAL,
  IN CONST CHAR8        *Line
);

// Returns a LOG_SINK that writes through QuirksToAbl to Log, which may be
// NULL.
LOG_SINK
QuirksAblLog (IN OPENABL_STORAGE_LOG Log OPTIONAL);

// Reads the settings the way the ABL reads them. A variable that
// isn't one byte counts as unset. Switches are 1 when set and nonzero. The
// boot source is mapped from the settings' numbering (Auto 0, Internal 1,
// SD card 2, USB 3) to devinfo's, Auto when unset. The charger screen is 0,
// 1 or QUIRKS_SETTING_UNSET.
QUIRKS_ABL_SETTINGS
QuirksAblSettings (VOID);

// Installs the ABL's storage protocol (OPENABL_STORAGE_PROTOCOL) on a new
// handle for the rest of boot services, so the menu must stay loaded. Chip
// is kept for the calls.
EFI_STATUS
QuirksInstallProtocol (IN UINT32 Chip);

// XBL's SecurityStubDxe has edk2's Defer3rdPartyImageLoad, which refuses
// images from a drive until EndOfDxe, so the EFI images the menu boots
// would be refused. Replaces the Security and Security2 checks with ones
// that accept every image, once a boot. The menu's own load (from a buffer)
// passes the stock check, so this waits until the menu knows it stays
// loaded, as the new checks are its own functions. Logs one line.
VOID
SecurityAcceptAll (IN LOG_SINK Log);

#endif
