/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  UsbHost - USB host mode for booting from a USB drive on Qualcomm XBL
  firmware, and the kernel devicetree fix-up for a USB boot.

  UsbHostBringUp switches the DWC3 core from the fastboot gadget to host
  mode through XBL's UsbConfig protocol, sources VBUS, starts an XHCI stack
  (XBL's own, or on SM6115 the ABL's edk2 drivers) and waits for a drive's
  filesystem. What it turns on stays on until it is given back, so the menu
  must stay loaded meanwhile. Host mode can come up again after a teardown
  in the same boot. Log lines start with "usb: ".
*/

#ifndef __EFI_USB_HOST_H__
#define __EFI_USB_HOST_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Protocol/OpenAblMenuHost.h>

#include "Core/UsbHostRegs.h"
#include "Efi/Serial.h"

// 4c7a91e2-5d3b-4f86-b0e9-1a2d6c8f3e57.
// 4c7a91e2-5d3b-4f86-b0e9-1a2d6c8f3e57.
#define USB_HOST_PROTOCOL_GUID                                                 \
  {0x4c7a91e2, 0x5d3b, 0x4f86, {0xb0, 0xe9, 0x1a, 0x2d, 0x6c, 0x8f, 0x3e, 0x57}}
#define USB_HOST_PROTOCOL_REVISION 0x00010001u
// Flags bit: host mode is up with a drive enumerated.
#define USB_HOST_FLAG_UP (1u << 0)

// Lets the ABL control USB host after the bring-up, while the menu stays
// loaded. No ABL header mirrors it, as the ABL no longer calls it.
typedef struct _USB_HOST_PROTOCOL {
  UINT32 Revision;
  UINT32 Flags;
  // Nothing booted: back to device mode, VBUS off.
  VOID (EFIAPI *Teardown) (IN struct _USB_HOST_PROTOCOL *This);
  // Before a power-off.
  VOID (EFIAPI *ReleaseVbus) (IN struct _USB_HOST_PROTOCOL *This);
  // Before the OS starts (also done at ExitBootServices).
  VOID (EFIAPI *OsHandoff) (IN struct _USB_HOST_PROTOCOL *This);
  // 1.1: brings host mode up again after a teardown, with the GPT quirk
  // before and the FAT quirk after. Log may be NULL. EFI_SUCCESS with a
  // drive up, EFI_NOT_FOUND without, EFI_NOT_READY before any bring-up.
  EFI_STATUS (EFIAPI *BringUp) (
    IN struct _USB_HOST_PROTOCOL *This,
    IN OPENABL_STORAGE_LOG        Log OPTIONAL
  );
} USB_HOST_PROTOCOL;

_Static_assert (
  offsetof (
    USB_HOST_PROTOCOL,
    OsHandoff
  ) == 0x18,
  "USB_HOST_PROTOCOL: the ABL's layout"
);
_Static_assert (
  offsetof (
    USB_HOST_PROTOCOL,
    BringUp
  ) == 0x20,
  "USB_HOST_PROTOCOL: the ABL's layout"
);

typedef struct _USB_HOST_OUTCOME {
  // A USB drive is enumerated and its filesystems are present.
  BOOLEAN Up;
  // Something is on (VBUS, the ADSP, host mode) that the ExitBootServices
  // callback turns off, so the menu has to stay loaded.
  BOOLEAN Resident;
} USB_HOST_OUTCOME;

// Brings USB host up on Board to boot from a USB drive, also after a
// teardown. Without a drive, VBUS goes off again. If something stays on,
// registers an ExitBootServices callback (once) that shuts down the ADSP
// and turns VBUS off.
USB_HOST_OUTCOME
UsbHostBringUp (
  IN USB_HOST_BOARD Board,
  IN LOG_SINK       Log
);

// Teardown, ReleaseVbus and OsHandoff log nothing, as whoever logged the
// bring-up may be gone.

// Unbinds the stack and returns to device mode with VBUS off, when nothing
// booted. Does nothing unless host is up.
VOID
UsbHostTeardown (VOID);

// Turns VBUS off before a power-off, if the menu turned it on. Host then
// counts as down.
VOID
UsbHostReleaseVbus (VOID);

// TRUE if host mode is up with a drive enumerated.
BOOLEAN
UsbHostIsUp (VOID);

// Shuts down the ADSP the bring-up loaded, before the OS loads its own.
VOID
UsbHostOsHandoff (VOID);

// Installs the protocol for the ABL on a new handle, once; later calls only
// update its flags. It is never freed, so the menu must stay loaded.
EFI_STATUS
UsbHostInstallProtocol (VOID);

// Puts the DWC3 controller in host mode after a USB boot, so the kernel
// keeps the drive it runs from: sets dr_mode = "host" on the first node
// compatible with "snps,dwc3" or "qcom,snps-dwc3". Skipped on SM6115, whose
// USB host is the ABL's own XHCI stack. Logs one line.
VOID
UsbBootFixDeviceTree (
  IN OUT UINT8 *Fdt,
  IN UINTN      Capacity,
  IN UINT32     Chip,
  IN LOG_SINK   Log
);

#endif
