/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  UsbHostRegs - USB host on Qualcomm SoCs, the parts that need no firmware.
*/

#include "Core/UsbHostRegs.h"

#include "Base/Print.h"
#include "Base/Text.h"

BOOLEAN
UsbHostBoardOf (
  IN UINT32           ChipId,
  IN CONST CHAR8     *Name OPTIONAL,
  OUT USB_HOST_BOARD *Out
)
{
  STATIC CONST CHAR8 *CONST Glink[] = {"SM4450", "SM6450", "SM8475", "SM8550",
                                       "SM8650", "SM8750"};
  OABL_TEXT                 Trimmed;

  switch (ChipId) {
  case 356:
    *Out = USB_HOST_BOARD_SM8250;
    return TRUE;
  // Snapdragon 662, and QCS6125, the Kamorta IoT part.
  case 444:
  case 467:
    *Out = USB_HOST_BOARD_SM6115;
    return TRUE;
  // G1 Gen 2 (SM4450), G2 Gen 1 (SM6450), G2 Gen 2 (Cliffs XBL, with
  // PmicGlinkDxe and the ADSP, DWC3 at 0xa600000), 8+ Gen 1 (SM8475),
  // QCS8550 and G3x Gen 2, G3 Gen 3 (SM8650), 8 Elite and Q8750.
  case 654:
  case 634:
  case 700:
  case 531:
  case 601:
  case 603:
  case 682:
  case 639:
  case 706:
    *Out = USB_HOST_BOARD_GLINK;
    return TRUE;
  }
  Trimmed = TextTrim (TextOf (Name));
  if (TextStartsWith (Trimmed, "SM8250")) {
    *Out = USB_HOST_BOARD_SM8250;
    return TRUE;
  }
  if (TextStartsWith (Trimmed, "SM6115")) {
    *Out = USB_HOST_BOARD_SM6115;
    return TRUE;
  }
  for (UINTN Index = 0; Index < sizeof (Glink) / sizeof (*Glink); Index++) {
    if (TextStartsWith (Trimmed, Glink[Index])) {
      *Out = USB_HOST_BOARD_GLINK;
      return TRUE;
    }
  }
  return FALSE;
}

UINT64
UsbHostBoardXhciBaseFallback (IN USB_HOST_BOARD Board)
{
  return Board == USB_HOST_BOARD_SM6115 ? 0x04E00000ULL : 0x0A600000ULL;
}

BOOLEAN
UsbHostBoardEudEnable (
  IN USB_HOST_BOARD Board,
  OUT UINT64       *Addr
)
{
  if (Board != USB_HOST_BOARD_SM6115) {
    return FALSE;
  }
  *Addr = 0x01612000ULL;
  return TRUE;
}

BOOLEAN
UsbHostBoardBundledStack (IN USB_HOST_BOARD Board)
{
  return Board == USB_HOST_BOARD_SM6115;
}

CONST CHAR8 *
UsbHostBoardName (IN USB_HOST_BOARD Board)
{
  switch (Board) {
  case USB_HOST_BOARD_SM6115:
    return "SM6115";
  case USB_HOST_BOARD_SM8250:
    return "SM8250";
  case USB_HOST_BOARD_GLINK:
  default:
    return "SM4450/SM6450/SM8475/SM8550+ (ADSP)";
  }
}

BOOLEAN
UsbHostUsbConfigVbusSlots (
  IN UINT32  Revision,
  OUT UINTN *Status,
  OUT UINTN *Enable
)
{
  if (Revision >= 0x00010005) {
    *Status = 0x98;
    *Enable = 0xA0;
    return TRUE;
  }
  if (Revision >= 0x00010003 && Revision <= 0x00010004) {
    *Status = 0x90;
    *Enable = 0x98;
    return TRUE;
  }
  return FALSE;
}

STATIC VOID
SetBits (
  IN CONST MMIO *Io,
  IN UINT64      Addr,
  IN UINT32      Bits
)
{
  MmioWrite32 (Io, Addr, MmioRead32 (Io, Addr) | Bits);
}

STATIC VOID
ClearBits (
  IN CONST MMIO *Io,
  IN UINT64      Addr,
  IN UINT32      Bits
)
{
  MmioWrite32 (Io, Addr, MmioRead32 (Io, Addr) & ~Bits);
}

BOOLEAN
UsbHostClearRunStop (
  IN CONST MMIO     *Io,
  IN UINT64          Base,
  IN USB_HOST_DELAY *DelayUs,
  IN VOID           *Context,
  OUT UINT32        *Before,
  OUT UINT32        *After
)
{
  UINT32 Dctl = MmioRead32 (Io, Base + USB_HOST_DWC3_DCTL);

  *Before = Dctl;
  if (!(Dctl & USB_HOST_DWC3_DCTL_RUN_STOP)) {
    return FALSE;
  }
  ClearBits (Io, Base + USB_HOST_DWC3_DCTL, USB_HOST_DWC3_DCTL_RUN_STOP);
  DelayUs (1000, Context);
  *After = MmioRead32 (Io, Base + USB_HOST_DWC3_DCTL);
  return TRUE;
}

BOOLEAN
UsbHostDisableEud (
  IN CONST MMIO     *Io,
  IN UINT64          Addr,
  IN USB_HOST_DELAY *DelayUs,
  IN VOID           *Context,
  OUT UINT32        *Eud
)
{
  *Eud = MmioRead32 (Io, Addr);
  if (!(*Eud & 1)) {
    return FALSE;
  }
  ClearBits (Io, Addr, 1);
  DelayUs (5000, Context);
  return TRUE;
}

VOID
UsbHostSetVbusValid (
  IN CONST MMIO *Io,
  IN UINT64      Base
)
{
  SetBits (
    Io, Base + USB_HOST_DWC3_QSCRATCH_SS_PHY_CTRL, USB_HOST_DWC3_SS_VBUS_VALID
  );
  SetBits (
    Io, Base + USB_HOST_DWC3_QSCRATCH_HS_PHY_CTRL, USB_HOST_DWC3_HS_VBUS_VALID
  );
}

VOID
UsbHostClearVbusValid (
  IN CONST MMIO *Io,
  IN UINT64      Base
)
{
  ClearBits (
    Io, Base + USB_HOST_DWC3_QSCRATCH_HS_PHY_CTRL, USB_HOST_DWC3_HS_VBUS_VALID
  );
  ClearBits (
    Io, Base + USB_HOST_DWC3_QSCRATCH_SS_PHY_CTRL, USB_HOST_DWC3_SS_VBUS_VALID
  );
}

UINT32
UsbHostForceHost (
  IN CONST MMIO     *Io,
  IN UINT64          Base,
  IN USB_HOST_DELAY *DelayUs,
  IN VOID           *Context
)
{
  UINT64 Gctl = Base + USB_HOST_DWC3_GCTL;
  UINT32 Before;

  SetBits (Io, Gctl, USB_HOST_DWC3_GCTL_CORESOFTRESET);
  SetBits (Io, Base + USB_HOST_DWC3_GUSB3PIPECTL, USB_HOST_DWC3_PHYSOFTRST);
  SetBits (Io, Base + USB_HOST_DWC3_GUSB2PHYCFG, USB_HOST_DWC3_PHYSOFTRST);
  DelayUs (100000, Context);
  ClearBits (Io, Base + USB_HOST_DWC3_GUSB3PIPECTL, USB_HOST_DWC3_PHYSOFTRST);
  ClearBits (Io, Base + USB_HOST_DWC3_GUSB2PHYCFG, USB_HOST_DWC3_PHYSOFTRST);
  DelayUs (100000, Context);
  ClearBits (Io, Gctl, USB_HOST_DWC3_GCTL_CORESOFTRESET);
  DelayUs (10000, Context);
  Before = MmioRead32 (Io, Gctl);
  MmioWrite32 (
    Io, Gctl,
    (Before & ~USB_HOST_DWC3_GCTL_PRTCAPDIR) |
      USB_HOST_DWC3_GCTL_PRTCAPDIR_HOST | USB_HOST_DWC3_GCTL_U2EXIT_LFPS
  );
  SetBits (Io, Base + USB_HOST_DWC3_GRXTHRCFG, USB_HOST_DWC3_GRXTHRCFG_HOST);
  SetBits (Io, Base + USB_HOST_DWC3_GSBUSCFG1, USB_HOST_DWC3_GSBUSCFG1_HOST);
  DelayUs (20000, Context);
  return Before;
}

USB_HOST_PORTSC
UsbHostPorts (
  IN CONST MMIO *Io,
  IN UINT64      Base
)
{
  USB_HOST_PORTSC Ports;

  // PORTSC1 first, then PORTSC2.
  Ports.Portsc1 = MmioRead32 (Io, Base + USB_HOST_DWC3_PORTSC1);
  Ports.Portsc2 = MmioRead32 (Io, Base + USB_HOST_DWC3_PORTSC2);
  return Ports;
}

BOOLEAN
UsbHostConnected (IN USB_HOST_PORTSC Ports)
{
  return ((Ports.Portsc1 | Ports.Portsc2) & USB_HOST_DWC3_PORTSC_CCS) != 0;
}

CHAR8 *
UsbHostHex (IN UINT64 Value)
{
  return SPrintAlloc ("0x%llx", (UINT64)Value);
}

CHAR8 *
UsbHostDec (IN UINT64 Value)
{
  return SPrintAlloc ("%llu", (UINT64)Value);
}

CHAR8 *
UsbHostHexBuf (
  OUT CHAR8 *Out,
  IN UINTN   Size,
  IN UINT64  Value
)
{
  SPrint (Out, Size, "0x%llx", (UINT64)Value);
  return Out;
}

CHAR8 *
UsbHostDecBuf (
  OUT CHAR8 *Out,
  IN UINTN   Size,
  IN UINT64  Value
)
{
  SPrint (Out, Size, "%llu", (UINT64)Value);
  return Out;
}
