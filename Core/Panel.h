/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Panel - XBL's panel configurations, and each model's panel power lines.
*/

#ifndef __CORE_PANEL_H__
#define __CORE_PANEL_H__

#include "Base/OablBase.h"

#include "Base/Text.h"

// XBL has an XML configuration for each panel it supports. A vendor panel
// may set <PanelOrientation>2</PanelOrientation> (MDP_ROTATE_180), and XBL
// then scans out the framebuffer rotated by 180 degrees. The devicetree
// rotation doesn't include that, so the menu has to add it.

BOOLEAN
PanelXmlActiveSize (
  IN CONST UINT8 *Xml,
  IN UINTN        Size,
  OUT UINT32     *Width,
  OUT UINT32     *Height
);

// Returns 180 if a panel of size Width x Height is rotated, else 0. Fails
// if no panel has that size. XBL doesn't say which panel it detected, but
// in the firmware checked, rotated panels are the only ones of their size.
BOOLEAN
PanelXmlFirmwareRotation (
  IN CONST OABL_BYTES *Xmls,
  IN UINTN             Count,
  IN UINT32            Width,
  IN UINT32            Height,
  OUT UINT16          *Rotation
);

// Returns the panel name for a Width x Height framebuffer, if only one
// name has that size.
BOOLEAN
PanelXmlNameAt (
  IN CONST OABL_BYTES *Xmls,
  IN UINTN             Count,
  IN UINT32            Width,
  IN UINT32            Height,
  OUT OABL_TEXT       *Name
);

// DisplayQuiesce bit 0: turn the display off through XBL's display power
// protocol before ExitBootServices, then its clocks and the MDSS core GDSC.
#define DISPLAY_HANDOVER_QUIESCE 1
// DisplayQuiesce bit 1: turn the panel lines off, the backlight before the
// display and the supplies after it.
#define DISPLAY_HANDOVER_LINES_OFF 2

// PMIC lines are turned off with SPMI writes, TLMM lines through XBL's TLMM
// protocol.
typedef enum {
  PANEL_LINE_TLMM,
  // Driven low by writing 0 to DIG_OUT_SOURCE_CTL, where XBL writes 0x80 to
  // drive it high. The kernel's pinctrl rewrites it when it claims the pad.
  PANEL_LINE_PMIC_GPIO,
  // The duty is zeroed and latched before the channel is disabled. If it is
  // only disabled, the kernel's first enable briefly shows XBL's old duty.
  PANEL_LINE_PWM,
  // PM8150L WLED. The sinks, then the module, are turned off, the same last
  // two writes as XBL's power-down.
  PANEL_LINE_WLED,
} PANEL_LINE_KIND;

// Only the fields used by Kind are meaningful.
typedef struct _PANEL_LINE {
  PANEL_LINE_KIND Kind;
  UINT16          Gpio;
  UINT8           Sid, Pid;
} PANEL_LINE;

typedef struct _PANEL_LINE_SPMI_WRITE {
  UINT8  Sid;
  UINT16 Addr;
  UINT8  Value;
} PANEL_LINE_SPMI_WRITE;

#define DISPLAY_HANDOVER_PMIC_GPIO_DIG_OUT_SOURCE_CTL 0x44
// Bit 7 enables the peripheral.
#define DISPLAY_HANDOVER_PMIC_EN_CTL 0x46
// Bit 0 of PWM_SYNC latches PWM_VALUE, as in mainline leds-qcom-lpg.
#define DISPLAY_HANDOVER_LPG_PWM_VALUE_LSB 0x44
#define DISPLAY_HANDOVER_LPG_PWM_VALUE_MSB 0x45
#define DISPLAY_HANDOVER_LPG_PWM_SYNC      0x47
#define DISPLAY_HANDOVER_WLED_SINK_EN      0xd946
#define DISPLAY_HANDOVER_WLED_MODULE_EN    0xd846

// Returns the SPMI writes that turn Line off, in order. TLMM lines need
// none.
UINTN
PanelLineSpmiOff (
  IN PANEL_LINE             Line,
  OUT PANEL_LINE_SPMI_WRITE Out[4]
);

typedef struct _PANEL_LINES {
  // As named by the device-model setting.
  CONST CHAR8 *Model;
  // Turned off before XBL's display-off stops DSI. Otherwise an LCD shows
  // white or static until its light goes out.
  CONST PANEL_LINE *Backlight;
  UINTN             BacklightCount;
  // Left on by XBL's display-off. Turned off after it.
  CONST PANEL_LINE *Supplies;
  UINTN             SupplyCount;
} PANEL_LINES;

// From the Linux devicetrees and the kernel's PMIC drivers, checked against
// what XBL's power-up drives.
extern CONST PANEL_LINES gPanelLines[];
extern CONST UINTN       gPanelLinesCount;

// Returns NULL if Model isn't listed. Case is ignored, as in the ABL.
CONST PANEL_LINES *
PanelLinesForModel (IN CONST CHAR8 *Model);

// Returns XBL's EFI_GPIO_CFG word for Gpio as a plain output, no pull,
// 2 mA.
UINT32
PanelLinesTlmmOutput (IN UINT16 Gpio);

#endif
