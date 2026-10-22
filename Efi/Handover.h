/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Handover - prepares the system just before a booted image starts.

  The hypervisor stops logging to the UART, the model's devicetree is
  installed as the EBBR configuration table, DRAM the firmware left out is
  given to the OS, and XBL regions unused after boot are freed. With
  OABL_LOADER_QUIRKS (SDM845) code allocations are made executable and
  SetVirtualAddressMap is refused.
*/

#ifndef __EFI_HANDOVER_H__
#define __EFI_HANDOVER_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Efi/Serial.h"

typedef struct _HANDOVER_PREPARE {
  // Drive the image comes from, searched for the model's devicetree. May be
  // NULL.
  EFI_HANDLE Fs;
  // The image brings its own devicetree.
  BOOLEAN OwnDevicetree;
  // Selected device model, or "" for none.
  CONST CHAR8 *Model;
  // ABL verbose mode. XBL's log and ramdump regions are kept.
  BOOLEAN DevMode;
  // A VM runs under the hypervisor (the ABL's IsVmEnabled).
  BOOLEAN Vm;
  // The ABL's version ("1.2"), for the ESRT.
  CONST CHAR8 *Version;
} HANDOVER_PREPARE;

// Called just before an image starts: installs EFI_DT_FIXUP_PROTOCOL, the
// ESRT, the runtime properties table where the chip needs one and the
// model's devicetree from Fs unless the image brings its own, and frees
// XBL's kernel staging region until HandoverBootReturned.
VOID
HandoverPrepare (
  IN CONST HANDOVER_PREPARE *Prepare,
  IN LOG_SINK                Log
);

// Called when the image returns. Reserves the staging region again.
VOID
HandoverBootReturned (IN LOG_SINK Log);

// Asks the hypervisor to stop logging to the UART the OS takes over. Same
// call as the ABL's DisableHypUartUsageForLogging: SMCCC fast call
// 0x86000000, which may clobber x0-x17.
VOID
HandoverHypUartOff (VOID);

// Installs Dtb as the devicetree table (gFdtTableGuid) in ACPI-reclaim pages
// with 8 KiB of padding and its splash region pointed at XBL's framebuffer,
// replacing any table installed earlier. Size and At get the installed blob;
// returns NULL on success, else a static error string.
CONST CHAR8 *
HandoverInstallDevicetree (
  IN CONST UINT8  *Dtb,
  IN UINTN         DtbSize,
  IN LOG_SINK      Log,
  OUT UINTN       *Size,
  OUT CONST VOID **At
);

// Gets XBL's splash framebuffer region, "Display Reserved" or
// "Display_Reserved". The panel may still be scanning it out.
BOOLEAN
HandoverDisplayRegion (
  OUT UINT64 *Base,
  OUT UINT64 *Len
);

#endif
