/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Panel - XBL's panel configurations, and each model's panel power lines.
*/

#include "Core/Panel.h"

#include "Base/Print.h"

#include "Base/Text.h"

// XBL panel XML

// MDP_ROTATE_180 in XBL's MDP_RotateFlipType.
#define MDP_ROTATE_180 2

// Returns the trimmed text between <Name> and </Name>, if it is UTF-8.
STATIC BOOLEAN
Tag (
  IN OABL_BYTES   Xml,
  IN CONST CHAR8 *Name,
  OUT OABL_TEXT  *Out
)
{
  OABL_TEXT Rest = {(CONST CHAR8 *)Xml.Ptr, Xml.Len};
  CHAR8     Open[32];
  CHAR8     Close[32];
  UINTN     Start;
  UINTN     Len;

  SPrint (Open, sizeof (Open), "<%s>", Name);
  SPrint (Close, sizeof (Close), "</%s>", Name);
  if (!TextFind (Rest, Open, &Start)) {
    return FALSE;
  }
  Rest = TextSub (Rest, Start + AsciiStrLen (Open), Rest.Len);
  if (!TextFind (Rest, Close, &Len) ||
      !Utf8Valid ((CONST UINT8 *)Rest.Ptr, Len)) {
    return FALSE;
  }
  *Out = TextTrim (TextSub (Rest, 0, Len));
  return TRUE;
}

// Parses a decimal number, or a hexadecimal one after 0x or 0X.
STATIC BOOLEAN
Number (
  IN OABL_TEXT Text,
  OUT UINT32  *Value
)
{
  UINT64  Parsed;
  BOOLEAN Ok;

  if (TextStartsWith (Text, "0x") || TextStartsWith (Text, "0X")) {
    Ok = TextParseU64 (TextSub (Text, 2, Text.Len), 16, &Parsed);
  } else {
    Ok = TextParseU64 (Text, 10, &Parsed);
  }
  if (!Ok || Parsed > MAX_UINT32) {
    return FALSE;
  }
  *Value = (UINT32)Parsed;
  return TRUE;
}

STATIC BOOLEAN
Sized (
  IN OABL_BYTES Xml,
  IN UINT32     Width,
  IN UINT32     Height
)
{
  UINT32 W;
  UINT32 H;

  return PanelXmlActiveSize (Xml.Ptr, Xml.Len, &W, &H) && W == Width &&
         H == Height;
}

BOOLEAN
PanelXmlActiveSize (
  IN CONST UINT8 *Xml,
  IN UINTN        Size,
  OUT UINT32     *Width,
  OUT UINT32     *Height
)
{
  OABL_BYTES Bytes = BytesOf (Xml, Size);
  OABL_TEXT  Text;
  UINT32     W;
  UINT32     H;

  if (!Tag (Bytes, "HorizontalActive", &Text) || !Number (Text, &W) ||
      !Tag (Bytes, "VerticalActive", &Text) || !Number (Text, &H)) {
    return FALSE;
  }
  *Width  = W;
  *Height = H;
  return TRUE;
}

BOOLEAN
PanelXmlFirmwareRotation (
  IN CONST OABL_BYTES *Xmls,
  IN UINTN             Count,
  IN UINT32            Width,
  IN UINT32            Height,
  OUT UINT16          *Rotation
)
{
  BOOLEAN Seen = FALSE;

  for (UINTN Index = 0; Index < Count; Index++) {
    OABL_TEXT Text;
    UINT32    Orientation;

    if (!Sized (Xmls[Index], Width, Height)) {
      continue;
    }
    Seen = TRUE;
    if (Tag (Xmls[Index], "PanelOrientation", &Text) &&
        Number (Text, &Orientation) && Orientation == MDP_ROTATE_180) {
      *Rotation = 180;
      return TRUE;
    }
  }
  if (Seen) {
    *Rotation = 0;
  }
  return Seen;
}

BOOLEAN
PanelXmlNameAt (
  IN CONST OABL_BYTES *Xmls,
  IN UINTN             Count,
  IN UINT32            Width,
  IN UINT32            Height,
  OUT OABL_TEXT       *Name
)
{
  OABL_TEXT Found = {0};
  OABL_TEXT Text;
  BOOLEAN   Any = FALSE;

  for (UINTN Index = 0; Index < Count; Index++) {
    if (!Sized (Xmls[Index], Width, Height) ||
        !Tag (Xmls[Index], "PanelName", &Text) || !Text.Len) {
      continue;
    }
    if (Any && !TextEq (Found, Text)) {
      return FALSE;
    }
    Found = Text;
    Any   = TRUE;
  }
  if (Any) {
    *Name = Found;
  }
  return Any;
}

// Panel lines

#define TLMM(Number) {.Kind = PANEL_LINE_TLMM, .Gpio = (Number)}
#define PMIC_GPIO(SlaveId, Peripheral)                                         \
  {.Kind = PANEL_LINE_PMIC_GPIO, .Sid = (SlaveId), .Pid = (Peripheral)}
#define PWM(SlaveId, Peripheral)                                               \
  {.Kind = PANEL_LINE_PWM, .Sid = (SlaveId), .Pid = (Peripheral)}
#define WLED(SlaveId) {.Kind = PANEL_LINE_WLED, .Sid = (SlaveId)}

// An array and its length, or NONE.
#define LINES(Array) (Array), sizeof (Array) / sizeof (*(Array))
#define NONE         NULL, 0

UINTN
PanelLineSpmiOff (
  IN PANEL_LINE             Line,
  OUT PANEL_LINE_SPMI_WRITE Out[4]
)
{
  UINT16 Base = (UINT16)(Line.Pid << 8);

  switch (Line.Kind) {
  case PANEL_LINE_TLMM:
    return 0;
  case PANEL_LINE_PMIC_GPIO:
    Out[0] = (PANEL_LINE_SPMI_WRITE){
      Line.Sid, Base | DISPLAY_HANDOVER_PMIC_GPIO_DIG_OUT_SOURCE_CTL, 0};
    return 1;
  case PANEL_LINE_PWM:
    Out[0] = (PANEL_LINE_SPMI_WRITE){
      Line.Sid, Base | DISPLAY_HANDOVER_LPG_PWM_VALUE_LSB, 0};
    Out[1] = (PANEL_LINE_SPMI_WRITE){
      Line.Sid, Base | DISPLAY_HANDOVER_LPG_PWM_VALUE_MSB, 0};
    Out[2] = (PANEL_LINE_SPMI_WRITE){Line.Sid,
                                     Base | DISPLAY_HANDOVER_LPG_PWM_SYNC, 1};
    Out[3] =
      (PANEL_LINE_SPMI_WRITE){Line.Sid, Base | DISPLAY_HANDOVER_PMIC_EN_CTL, 0};
    return 4;
  case PANEL_LINE_WLED:
    Out[0] =
      (PANEL_LINE_SPMI_WRITE){Line.Sid, DISPLAY_HANDOVER_WLED_SINK_EN, 0};
    Out[1] =
      (PANEL_LINE_SPMI_WRITE){Line.Sid, DISPLAY_HANDOVER_WLED_MODULE_EN, 0};
    return 2;
  }
  return 0;
}

// RP6 backlight: PMK8550 GPIO 5 (0xbc on slave 0), which XBL drives high,
// and PMK8550 PWM 1 (pmk8550_pwm 0, LPG at 0xe800), left to pwm-backlight.
STATIC CONST PANEL_LINE mRp6Backlight[] = {PMIC_GPIO (0, 0xbc), PWM (0, 0xe8)};
// RP6 1.8 V (70) and 2.8 V (142) enables, left high by XBL's display-off.
STATIC CONST PANEL_LINE mRp6Supplies[] = {TLMM (70), TLMM (142)};
// Air X backlight: PM6125 PWM (pm6125_pwm 0), the LPG channel at 0xb300 in
// the Linux driver, on the PMIC's second slave id.
STATIC CONST PANEL_LINE mAirXBacklight[] = {PWM (1, 0xb3)};
// Air X panel enable (97) and the AW37503/SGM3804 bias enables 44 (AVDD)
// and 45 (AVEE).
STATIC CONST PANEL_LINE mAirXSupplies[] = {TLMM (97), TLMM (44), TLMM (45)};

// SM8250: the backlight is the PM8150L WLED on the PMIC's second slave id
// (5). XBL's power-down turns it off last and never touches the TLMM.
// panel_en is TLMM 14, display AVDD is TLMM 61.
STATIC CONST PANEL_LINE mWled5[]             = {WLED (5)};
STATIC CONST PANEL_LINE mPocketMaxSupplies[] = {TLMM (14), TLMM (61)};
STATIC CONST PANEL_LINE mAirYProSupplies[]   = {TLMM (14)};
// The RP5 AMOLED has no backlight. vdda_panel is TLMM 4.
STATIC CONST PANEL_LINE mRp5Supplies[] = {TLMM (4)};
// SM8550: the backlight 5 V (52) and the 2.8 V (142) enables stay high
// after XBL's power-down.
STATIC CONST PANEL_LINE mOdin2PortalBacklight[] = {TLMM (52)};
STATIC CONST PANEL_LINE mOdin2PortalSupplies[]  = {TLMM (142)};
// SM8650: the SY7758 backlight is on I2C with no enable line. XBL already
// drives the SGM3804 enables (59 AVDD, 58 AVEE) low; they are listed for a
// hand-over without the display-off.
STATIC CONST PANEL_LINE mPocketFitSupplies[] = {TLMM (59), TLMM (58)};
// SM8750: the backlight 5 V (56) and 2.8 V (97) enables stay high.
STATIC CONST PANEL_LINE mOdin3Backlight[] = {TLMM (56)};
STATIC CONST PANEL_LINE mOdin3Supplies[]  = {TLMM (97)};
// SM4450 rev Q: AMOLED, no backlight (its PM6450 PWM drives the fan).
// 1.8 V (30), 2.8 V (3) and AVDD (127) on the TLMM. vddio and extdvdd are
// PM7250B GPIOs 11 and 12 (peripherals 0xca and 0xcb, slave 4).
STATIC CONST PANEL_LINE mClassicQSupplies[] = {
  TLMM (30), TLMM (3), TLMM (127), PMIC_GPIO (4, 0xca), PMIC_GPIO (4, 0xcb),
};

CONST PANEL_LINES gPanelLines[] = {
  {"MANGMI Pocket Max", LINES (mWled5), LINES (mPocketMaxSupplies)},
  {"MANGMI Air Y Pro", LINES (mWled5), LINES (mAirYProSupplies)},
  {"Retroid Pocket 5", NONE, LINES (mRp5Supplies)},
  {"AYN Odin 2 Portal", LINES (mOdin2PortalBacklight),
   LINES (mOdin2PortalSupplies)},
  {"Retroid Pocket 6", LINES (mRp6Backlight), LINES (mRp6Supplies)},
  // The Nova is the RP6 board with another panel: same lines.
  {"Retroid Pocket Nova", LINES (mRp6Backlight), LINES (mRp6Supplies)},
  {"KONKR Pocket FIT", NONE, LINES (mPocketFitSupplies)},
  {"AYN Odin 3", LINES (mOdin3Backlight), LINES (mOdin3Supplies)},
  {"Retroid Pocket Classic Rev Q", NONE, LINES (mClassicQSupplies)},
  // SM6115.
  {"MANGMI Air X MQ65", LINES (mAirXBacklight), LINES (mAirXSupplies)},
  {"MANGMI Air X MQ66", LINES (mAirXBacklight), LINES (mAirXSupplies)},
  {"MANGMI Air X MQ66B", LINES (mAirXBacklight), LINES (mAirXSupplies)},
  // The Air Y is the Air X board with the GH7005 panel: same PWM, enable 97
  // and bias 44/45.
  {"MANGMI Air Y", LINES (mAirXBacklight), LINES (mAirXSupplies)},
};

CONST UINTN gPanelLinesCount = sizeof (gPanelLines) / sizeof (*gPanelLines);

CONST PANEL_LINES *
PanelLinesForModel (IN CONST CHAR8 *Model)
{
  OABL_TEXT Wanted = TextOf (Model);

  for (UINTN Index = 0; Index < gPanelLinesCount; Index++) {
    if (TextEqNoCase (Wanted, gPanelLines[Index].Model)) {
      return &gPanelLines[Index];
    }
  }
  return NULL;
}

UINT32
PanelLinesTlmmOutput (IN UINT16 Gpio)
{
  CONST UINT32 Output = 1;

  return ((UINT32)Gpio & 0x3ff) << 4 | Output << 14 | 0x20000000;
}
