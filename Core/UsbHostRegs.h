/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  UsbHostRegs - USB host on Qualcomm SoCs, the parts that need no firmware.

  Picks the bring-up recipe for a chip and holds the DWC3 register
  sequences, which go through an MMIO so that host tests can check them.
  Efi/UsbHost.c does the firmware side.
*/

#ifndef __CORE_USB_HOST_REGS_H__
#define __CORE_USB_HOST_REGS_H__

#include "Base/OablBase.h"

#include "Core/Mmio.h"

// How a SoC gets USB host: who supplies VBUS, and whose XHCI stack runs.
typedef enum {
  // SM6115 (Kamorta). XBL has no host stack, so the ABL's own edk2 drivers
  // (NonDiscoverablePci, Xhci, UsbBus, UsbMass) run on the DWC3 core forced
  // into host mode. VBUS comes from the PMIC SCHG, then the SGM41515
  // charger over I2C as wired on the Mangmi Air X.
  USB_HOST_BOARD_SM6115,
  // SM8250 (Kona). XBL's own host stack, VBUS from the SCHG of PMIC 1.
  USB_HOST_BOARD_SM8250,
  // SM4450, SM6450, G2 Gen 2, SM8550, SM8650, SM8750. XBL's own host stack.
  // The charger is behind qcom,pmic_glink, so VBUS goes through the ADSP:
  // load it (PIL FULL_ADSP) and check its PMIC glink channel.
  USB_HOST_BOARD_GLINK,
} USB_HOST_BOARD;

// Picks the recipe by ChipInfo id, else by Name. Fails if there is none.
BOOLEAN
UsbHostBoardOf (
  IN UINT32           ChipId,
  IN CONST CHAR8     *Name OPTIONAL,
  OUT USB_HOST_BOARD *Out
);

// DWC3 core base address, for when UsbConfig doesn't give one.
UINT64
UsbHostBoardXhciBaseFallback (IN USB_HOST_BOARD Board);

// Returns the Embedded USB Debugger enable register, if the board has one.
BOOLEAN
UsbHostBoardEudEnable (
  IN USB_HOST_BOARD Board,
  OUT UINT64       *Addr
);

BOOLEAN
UsbHostBoardBundledStack (IN USB_HOST_BOARD Board);

CONST CHAR8 *
UsbHostBoardName (IN USB_HOST_BOARD Board);

// Size of the DWC3 register window given to the XHCI driver.
#define USB_HOST_XHCI_WINDOW 0x00100000ULL

// XBL's QCOM_USB_CONFIG_PROTOCOL. All revisions match up to GetSupUsbMode.

// Modes for GetUsbHostConfig and StartController.
#define USB_HOST_USB_CONFIG_HOST_MODE_XHCI 1u
#define USB_HOST_USB_CONFIG_DEVICE_MODE_SS 4u
// StopController with this stops nothing, as wanted. DEVICE_MODE_SS or
// ToggleUsbMode would free the gadget under UsbDeviceDxe on MXF.2.5.
#define USB_HOST_USB_CONFIG_DEVICE_MODE 0x10u
#define USB_HOST_USB_CONFIG_CORE_0      0u
// USB_CONFIG_CORE_MAX. A host core number at or above it is invalid.
#define USB_HOST_USB_CONFIG_CORE_MAX 8u
// From here on XBL reads EnableSLTLoopback on each StartController.
#define USB_HOST_USB_CONFIG_REVISION_SLT_UNLOCK 0x00010005u

// Returns the offsets of GetUsbVbusStatus and UsbEnableVbus. Revision
// 0x10005 and later (SM8250 and SM8550 on, SM8750 reports 0x20001 or
// 0x30001) have an AlwaysConnected byte before them; 0x10003 and 0x10004
// (SDM670, SM6115) don't. Other revisions fail.
BOOLEAN
UsbHostUsbConfigVbusSlots (
  IN UINT32  Revision,
  OUT UINTN *Status,
  OUT UINTN *Enable
);

// PMIC SCHG protocol, whose table is append-only. Revisions before MIN get
// no charger writes.
#define USB_HOST_SCHG_REVISION_MIN 0x0001000BULL
// Newest revision checked against XBL sources.
#define USB_HOST_SCHG_REVISION_MAX_VERIFIED 0x00010027ULL
// ConfigOtg takes DCDC enums and GetChargerPmicIndex exists, here only.
#define USB_HOST_SCHG_REVISION_DCDC_ENUMS 0x00010011ULL
// EFI_PM_SCHG_DCDC_OTG_CFG_OTG_EN_SRC_CFG.
#define USB_HOST_SCHG_OTG_EN_SRC_CFG  1u
#define USB_HOST_SCHG_OTG_ILIMIT_MA   500u
#define USB_HOST_SCHG_PMIC_INDEX_8250 1u

// DWC3 and Qualcomm wrapper registers, relative to the core base.
#define USB_HOST_DWC3_GSBUSCFG1    0xC104ULL
#define USB_HOST_DWC3_GRXTHRCFG    0xC10CULL
#define USB_HOST_DWC3_GCTL         0xC110ULL
#define USB_HOST_DWC3_GUSB2PHYCFG  0xC200ULL
#define USB_HOST_DWC3_GUSB3PIPECTL 0xC2C0ULL
#define USB_HOST_DWC3_DCTL         0xC704ULL
// Qualcomm QSCRATCH block, which has the VBUS-valid overrides.
#define USB_HOST_DWC3_QSCRATCH_HS_PHY_CTRL 0xF8810ULL
#define USB_HOST_DWC3_QSCRATCH_SS_PHY_CTRL 0xF8830ULL
// XHCI operational registers (CAPLENGTH is 0x20 on these cores).
#define USB_HOST_DWC3_XHCI_OP 0x20ULL
#define USB_HOST_DWC3_PORTSC1 (USB_HOST_DWC3_XHCI_OP + 0x400)
#define USB_HOST_DWC3_PORTSC2 (USB_HOST_DWC3_XHCI_OP + 0x410)

#define USB_HOST_DWC3_DCTL_RUN_STOP       (1u << 31)
#define USB_HOST_DWC3_GCTL_CORESOFTRESET  (1u << 11)
#define USB_HOST_DWC3_GCTL_PRTCAPDIR      (3u << 12)
#define USB_HOST_DWC3_GCTL_PRTCAPDIR_HOST (1u << 12)
#define USB_HOST_DWC3_GCTL_U2EXIT_LFPS    (1u << 2)
#define USB_HOST_DWC3_PHYSOFTRST          (1u << 31)
// USB max RX burst 3 and RX packet count 3, with the count enabled.
#define USB_HOST_DWC3_GRXTHRCFG_HOST ((3u << 19) | (3u << 24) | (1u << 29))
// Pipelined transfer limit 14, 1 KiB page boundary (EN1KPAGE).
#define USB_HOST_DWC3_GSBUSCFG1_HOST ((0xEu << 8) | (1u << 12))
// VBUS valid and its override in the HS and SS PHY controls.
#define USB_HOST_DWC3_HS_VBUS_VALID ((1u << 20) | (1u << 28))
#define USB_HOST_DWC3_SS_VBUS_VALID (1u << 24)
#define USB_HOST_DWC3_PORTSC_CCS    1u

// Waits Microseconds. Tests record the calls instead.
typedef VOID
USB_HOST_DELAY (
  IN UINT32 Microseconds,
  IN VOID  *Context
);

// Stops the device controller if XBL left it running, since XBL's host init
// skips the core reset and PHY setup while DCTL.RUN_STOP is set. Returns
// TRUE if it was cleared. Before and After get DCTL.
BOOLEAN
UsbHostClearRunStop (
  IN CONST MMIO     *Io,
  IN UINT64          Base,
  IN USB_HOST_DELAY *DelayUs,
  IN VOID           *Context,
  OUT UINT32        *Before,
  OUT UINT32        *After
);

// Turns the EUD off, since it holds the PHY. Returns TRUE if it was on.
BOOLEAN
UsbHostDisableEud (
  IN CONST MMIO     *Io,
  IN UINT64          Addr,
  IN USB_HOST_DELAY *DelayUs,
  IN VOID           *Context,
  OUT UINT32        *Eud
);

// Sets the VBUS-valid overrides. Without them SM6115 never sees a device.
VOID
UsbHostSetVbusValid (
  IN CONST MMIO *Io,
  IN UINT64      Base
);

// Undoes UsbHostSetVbusValid.
VOID
UsbHostClearVbusValid (
  IN CONST MMIO *Io,
  IN UINT64      Base
);

// Resets the core and PHYs and forces host mode, since XBL leaves SM6115 in
// device mode. Returns GCTL from before the change.
UINT32
UsbHostForceHost (
  IN CONST MMIO     *Io,
  IN UINT64          Base,
  IN USB_HOST_DELAY *DelayUs,
  IN VOID           *Context
);

typedef struct _USB_HOST_PORTSC {
  UINT32 Portsc1;
  UINT32 Portsc2;
} USB_HOST_PORTSC;

USB_HOST_PORTSC
UsbHostPorts (
  IN CONST MMIO *Io,
  IN UINT64      Base
);

BOOLEAN
UsbHostConnected (IN USB_HOST_PORTSC Ports);

// Bring-up timeouts, in milliseconds.

// Give up when nothing is plugged in after this long.
#define USB_HOST_WAIT_NO_CONNECT 2000u
// Whole bring-up.
#define USB_HOST_WAIT_TOTAL 15000u
// Wait for a filesystem after a device enumerates.
#define USB_HOST_WAIT_FILESYSTEM 6000u
// Connect all controllers this often.
#define USB_HOST_WAIT_CONNECT_EVERY 1500u

// Formats Value as "0x%llx" in new memory. Caller frees it.
CHAR8 *
UsbHostHex (IN UINT64 Value);

// Formats Value as "%llu" in new memory. Caller frees it.
CHAR8 *
UsbHostDec (IN UINT64 Value);

// Buffer size for either format of any value, including the NUL.
#define USB_HOST_NUM_SIZE 21

// Formats into Out, truncated to Size. Returns Out for use as an argument.
CHAR8 *
UsbHostHexBuf (
  OUT CHAR8 *Out,
  IN UINTN   Size,
  IN UINT64  Value
);

CHAR8 *
UsbHostDecBuf (
  OUT CHAR8 *Out,
  IN UINTN   Size,
  IN UINT64  Value
);

#endif
