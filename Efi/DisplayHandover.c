/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DisplayHandover - turns the display off, or not, before the OS starts.
*/

#include "Efi/DisplayHandover.h"

#include "Base/OablBase.h"

#include <Protocol/EFIDisplayPwr.h>

#include "Base/Print.h"
#include "Base/Text.h"
#include "Core/Panel.h"
#include "Core/Settings.h"
#include "Efi/Variables.h"

STATIC EFI_GUID mSettingsVendor = SETTINGS_VENDOR_GUID;

#define LINE_SIZE 160

// Formats a log line on the stack, truncated to fit.
STATIC VOID __attribute__ ((format (
  printf,
  2,
  3
)))
Say (
  IN OPENABL_STORAGE_LOG Log OPTIONAL,
  IN CONST CHAR8        *Format,
  ...
)
{
  CHAR8   Line[LINE_SIZE] = "openabl-menu: display: ";
  UINTN   PrefixLength    = AsciiStrLen (Line);
  va_list Args;

  if (!Log) {
    return;
  }
  va_start (Args, Format);
  VSPrint (Line + PrefixLength, sizeof (Line) - PrefixLength, Format, Args);
  va_end (Args);
  Log (Line);
}

// Reads setting Name into Buffer. Returns FALSE if it isn't set.
STATIC BOOLEAN
Setting (
  IN CONST CHAR8 *Name,
  OUT UINT8      *Buffer,
  IN UINTN        Capacity,
  OUT UINTN      *Length
)
{
  CHAR16 WideName[32];
  UINTN  Size = Capacity;
  UINTN  Index;

  for (Index = 0; Name[Index]; Index++) {
    if (Index + 1 >= sizeof (WideName) / sizeof (*WideName) ||
        (UINT8)Name[Index] >= 0x80) {
      return FALSE;
    }
    WideName[Index] = (CHAR16)Name[Index];
  }
  WideName[Index] = 0;
  if (gRT->GetVariable (WideName, &mSettingsVendor, NULL, &Size, Buffer) !=
      EFI_SUCCESS) {
    return FALSE;
  }
  *Length = Size;
  return TRUE;
}

BOOLEAN
DisplayHandoverKeepLogoSetting (VOID)
{
  UINT8 Bytes[8];
  UINTN Length;

  return (Setting (SETTINGS_KEY_KEEP_LOGO, Bytes, sizeof (Bytes), &Length) &&
          Length == 1 && Bytes[0] == 1) ||
         (DisplayHandoverQuiesceSetting () & DISPLAY_HANDOVER_KEEP_SPLASH) != 0;
}

UINT8
DisplayHandoverQuiesceSetting (VOID)
{
  UINT8 Bytes[8];
  UINTN Length;
  UINT8 Byte =
    Setting (SETTINGS_KEY_DISPLAY_QUIESCE, Bytes, sizeof (Bytes), &Length) &&
        Length
      ? Bytes[0]
      : 0;

#ifdef OABL_DISPLAY_QUIESCE
  return Byte;
#else
  return Byte & (UINT8)~(DISPLAY_HANDOVER_QUIESCE | DISPLAY_HANDOVER_LINES_OFF);
#endif
}

#ifdef OABL_DISPLAY_QUIESCE

// XBL's clock, TLMM and SPMI protocols. The ABL's tree has no headers for
// them.

STATIC EFI_GUID mClockGuid = {0x241afae6,
                              0x885f,
                              0x4f6c,
                              {0xa7, 0xea, 0xc2, 0x8e, 0xab, 0x79, 0xc3, 0xe5}};
STATIC EFI_GUID mTlmmGuid  = {0xad9aec18,
                              0x7bf0,
                              0x4809,
                              {0x9e, 0x96, 0x30, 0x12, 0x30, 0x9f, 0x3d, 0xf7}};
// gQcomSPMIProtocolGuid (EFISPMI.h), the same on XF and MXF.
STATIC EFI_GUID mSpmiGuid = {0xfa5f306b,
                             0xf47d,
                             0x4ac4,
                             {0xa4, 0x7d, 0x88, 0x2f, 0x82, 0x04, 0xec, 0x30}};

typedef struct _CLOCK_PROTOCOL CLOCK_PROTOCOL;
typedef EFI_STATUS (EFIAPI *CLOCK_NAME) (
  IN CLOCK_PROTOCOL *This,
  IN CONST CHAR8    *Name,
  OUT UINTN         *Id
);
typedef EFI_STATUS (EFIAPI *CLOCK_ID) (
  IN CLOCK_PROTOCOL *This,
  IN UINTN           Id
);

// EFI_CLOCK_PROTOCOL, up to the last function we call.
typedef struct _CLOCK_PROTOCOL {
  UINT64     Revision;
  CLOCK_NAME GetClockId;
  CLOCK_ID   EnableClock;
  CLOCK_ID   DisableClock;
  EFI_STATUS (EFIAPI *IsClockEnabled) (
    IN struct _CLOCK_PROTOCOL *This,
    IN UINTN                   Id,
    OUT BOOLEAN               *Enabled
  );
  UINTN IsClockOn;
  EFI_STATUS (EFIAPI *SetClockFreqHz) (
    IN struct _CLOCK_PROTOCOL *This,
    IN UINTN                   Id,
    IN UINT32                  FreqHz,
    IN UINT32                  Match,
    OUT UINT32                *ResultFreqHz
  );
  UINTN      GetClockFreqHz;
  UINTN      CalcClockFreqHz;
  UINTN      SelectExternalSource;
  CLOCK_NAME GetClockPowerDomainId;
  CLOCK_ID   EnableClockPowerDomain;
  CLOCK_ID   DisableClockPowerDomain;
} CLOCK_PROTOCOL;
// EFI_CLOCK_FREQUENCY_HZ_EXACT.
#define FREQ_EXACT 3u
#define XO_HZ      19200000u

// EFI_TLMM_PROTOCOL (its functions take no This).
typedef struct _TLMM_PROTOCOL {
  UINT64 Revision;
  EFI_STATUS (EFIAPI *ConfigGpio) (
    IN UINT32 Config,
    IN UINT32 Enable
  );
  UINTN ConfigGpioGroup;
  UINTN GpioIn;
  EFI_STATUS (EFIAPI *GpioOut) (
    IN UINT32 Config,
    IN UINT32 Value
  );
} TLMM_PROTOCOL;
#define TLMM_ENABLE 1u
#define GPIO_LOW    0u

// EFI_QCOM_SPMI_PROTOCOL, up to WriteLong. WriteLong returns 0 on success.
// XF builds (revision 0x10001: SM8250, SM6115) take (This, SlaveId,
// Priority, Register, Data, Len). MXF builds (0x10003: SM4450, SM8550 and
// later) add a BusId after This, for SoCs with two SPMI buses. Calling the
// MXF form the XF way faults in the driver.
typedef struct _SPMI_PROTOCOL {
  UINT64 Revision;
  UINTN  ReadLong;
  UINTN  WriteLong;
} SPMI_PROTOCOL;
typedef UINT32 (EFIAPI *SPMI_WRITE_LONG_XF) (
  IN SPMI_PROTOCOL *This,
  IN UINT32         SlaveId,
  IN UINT32         Priority,
  IN UINT32         Address,
  IN UINT8         *Data,
  IN UINT32         Len
);
typedef UINT32 (EFIAPI *SPMI_WRITE_LONG_MXF) (
  IN SPMI_PROTOCOL *This,
  IN UINT32         BusId,
  IN UINT32         SlaveId,
  IN UINT32         Priority,
  IN UINT32         Address,
  IN UINT8         *Data,
  IN UINT32         Len
);
#define SPMI_REVISION_BUS_ID 0x10003u
#define SPMI_PRIORITY_LOW    0u
#define SPMI_SUCCESS         0u
// Panel lines are all on bus 0. A second bus carries other PMICs.
#define SPMI_BUS 0u

// Offsets of the functions we call.
_Static_assert (
  offsetof (
    EFI_DISPLAY_POWER_PROTOCOL,
    SetDisplayPowerState
  ) == 8,
  "EFI_DISPLAY_POWER_PROTOCOL's layout"
);
_Static_assert (
  offsetof (
    CLOCK_PROTOCOL,
    SetClockFreqHz
  ) == 48,
  "EFI_CLOCK_PROTOCOL's layout"
);
_Static_assert (
  offsetof (
    CLOCK_PROTOCOL,
    DisableClockPowerDomain
  ) == 96,
  "EFI_CLOCK_PROTOCOL's layout"
);
_Static_assert (
  offsetof (
    TLMM_PROTOCOL,
    GpioOut
  ) == 32,
  "EFI_TLMM_PROTOCOL's layout"
);
_Static_assert (
  offsetof (
    SPMI_PROTOCOL,
    WriteLong
  ) == 16,
  "EFI_QCOM_SPMI_PROTOCOL's layout"
);

// Writes one byte to a PMIC register.
STATIC UINT32
SpmiWrite (
  IN SPMI_PROTOCOL *Spmi,
  IN UINT8          SlaveId,
  IN UINT16         Address,
  IN UINT8          Value
)
{
  UINT8 Data = Value;

  if (Spmi->Revision >= SPMI_REVISION_BUS_ID) {
    return ((SPMI_WRITE_LONG_MXF)Spmi->WriteLong) (
      Spmi, SPMI_BUS, SlaveId, SPMI_PRIORITY_LOW, Address, &Data, 1
    );
  }
  return ((SPMI_WRITE_LONG_XF)Spmi->WriteLong) (
    Spmi, SlaveId, SPMI_PRIORITY_LOW, Address, &Data, 1
  );
}

// Display clocks DisplayDxe can leave enabled when its power-off doesn't
// complete (SM6450). They are disabled in this order.
STATIC CONST CHAR8 *CONST mDisplayClocks[4] = {
  "disp_cc_mdss_rot_clk",
  "disp_cc_mdss_vsync_clk",
  "disp_cc_mdss_rscc_vsync_clk",
  "disp_cc_mdss_rscc_ahb_clk",
};
// The MDP clock's RCG stays on a display PLL unless the clock is set to XO
// before it is disabled.
STATIC CONST CHAR8 mMdpClock[] = "disp_cc_mdss_mdp_clk";
// The AHB clock goes last.
STATIC CONST CHAR8 mAhbClock[] = "disp_cc_mdss_ahb_clk";
// The MDSS core GDSC, as named in MXF and in XF builds.
STATIC CONST CHAR8 *CONST mCoreGdsc[2] = {"disp_cc_mdss_core_gdsc",
                                          "mdss_core_gdsc"};

// Names the statuses the steps meet. Others print as "error N" in Buffer.
STATIC CONST CHAR8 *
StatusString (
  IN EFI_STATUS Status,
  OUT CHAR8     Buffer[32]
)
{
  switch (Status) {
  case EFI_SUCCESS:
    return "Success";
  case EFI_NOT_FOUND:
    return "Not Found";
  case EFI_UNSUPPORTED:
    return "Unsupported";
  case EFI_INVALID_PARAMETER:
    return "Invalid Parameter";
  case EFI_DEVICE_ERROR:
    return "Device Error";
  }
  SPrint (Buffer, 32, "error %llu", (UINT64)(Status & ~((UINTN)1 << 63)));
  return Buffer;
}

// Turns the display off through XBL's display power protocol. This stops
// the timing engine, DSI and its PLL; what else it leaves on varies by SoC.
STATIC VOID
PowerOff (IN OPENABL_STORAGE_LOG Log OPTIONAL)
{
  EFI_DISPLAY_POWER_PROTOCOL *Power =
    LocateProtocol (&gEfiDisplayPowerStateProtocolGuid);
  EFI_DISPLAY_POWER_STATE State = 0;
  EFI_STATUS              Status;
  CHAR8                   Scratch[32];

  if (!Power) {
    Say (Log, "no display power protocol");
    return;
  }
  Status = Power->SetDisplayPowerState (Power, EfiDisplayPowerStateOff);
  (VOID)Power->GetDisplayPowerState (Power, &State);
  Say (
    Log, "display off: %s (state %u)", StatusString (Status, Scratch),
    (UINT32)State
  );
}

// Disables a clock if the driver still counts it enabled. With ToXo, it is
// first set to 19.2 MHz (exact match), which moves its RCG to XO. On SM8250 a
// disable alone doesn't do that for the MDP clock. Where the set is
// unsupported, as on SM6450, the disable alone is enough.
STATIC VOID
ParkClock (
  IN CLOCK_PROTOCOL     *Clock,
  IN CONST CHAR8        *Name,
  IN BOOLEAN             ToXo,
  IN OPENABL_STORAGE_LOG Log OPTIONAL
)
{
  UINTN      Id      = 0;
  BOOLEAN    Enabled = 0;
  EFI_STATUS Status  = Clock->GetClockId (Clock, Name, &Id);
  CHAR8      Scratch[32];

  if (Status != EFI_SUCCESS) {
    Say (
      Log, "%s: unknown to the clock driver (%s)", Name,
      StatusString (Status, Scratch)
    );
    return;
  }
  (VOID)Clock->IsClockEnabled (Clock, Id, &Enabled);
  if (ToXo) {
    UINT32 Got = 0;

    Status = Clock->SetClockFreqHz (Clock, Id, XO_HZ, FREQ_EXACT, &Got);
    Say (
      Log, "%s: set to XO: %s (%u Hz)", Name, StatusString (Status, Scratch),
      (UINT32)Got
    );
  }
  if (Enabled == 0) {
    Say (Log, "%s: already disabled", Name);
    return;
  }
  Status = Clock->DisableClock (Clock, Id);
  Say (Log, "%s: disabled: %s", Name, StatusString (Status, Scratch));
}

// Disables the display's clocks, then the MDSS core GDSC.
STATIC VOID
ParkClocks (IN OPENABL_STORAGE_LOG Log OPTIONAL)
{
  CLOCK_PROTOCOL *Clock = LocateProtocol (&mClockGuid);
  CHAR8           Scratch[32];

  if (!Clock) {
    Say (Log, "no clock protocol");
    return;
  }
  for (UINTN Index = 0;
       Index < sizeof (mDisplayClocks) / sizeof (*mDisplayClocks); Index++) {
    ParkClock (Clock, mDisplayClocks[Index], FALSE, Log);
  }
  ParkClock (Clock, mMdpClock, TRUE, Log);
  ParkClock (Clock, mAhbClock, FALSE, Log);
  for (UINTN Index = 0; Index < sizeof (mCoreGdsc) / sizeof (*mCoreGdsc);
       Index++) {
    UINTN Id = 0;

    if (Clock->GetClockPowerDomainId (Clock, mCoreGdsc[Index], &Id) ==
        EFI_SUCCESS) {
      EFI_STATUS Status = Clock->DisableClockPowerDomain (Clock, Id);

      Say (
        Log, "%s: disabled: %s", mCoreGdsc[Index],
        StatusString (Status, Scratch)
      );
      return;
    }
  }
  Say (Log, "no MDSS core power domain by name");
}

VOID
DisplayHandoverQuiesce (IN OPENABL_STORAGE_LOG Log OPTIONAL)
{
  Say (Log, "handing the display over parked");
  PowerOff (Log);
  ParkClocks (Log);
}

// Returns the panel lines for the selected model, or NULL (logged).
STATIC CONST PANEL_LINES *
LinesForModel (IN OPENABL_STORAGE_LOG Log OPTIONAL)
{
  CONST PANEL_LINES *Lines;
  CHAR8              Model[65];
  UINTN              Length;

  if (!Setting (SETTINGS_KEY_MODEL, (UINT8 *)Model, 64, &Length)) {
    Say (Log, "panel lines: no device model set");
    return NULL;
  }
  // Treat invalid UTF-8 as no model, and strip trailing NULs.
  if (!Utf8Valid ((CONST UINT8 *)Model, Length)) {
    Length = 0;
  }
  while (Length && Model[Length - 1] == 0) {
    Length--;
  }
  Model[Length] = 0;
  Lines         = PanelLinesForModel (Model);
  if (!Lines) {
    Say (Log, "panel lines: none known for \"%s\"", Model);
  }
  return Lines;
}

// Drives Lines off in order. TLMM lines go through XBL's TLMM protocol, PMIC
// lines through its SPMI protocol with the writes from PanelLineSpmiOff.
STATIC VOID
DriveOff (
  IN CONST CHAR8        *What,
  IN CONST PANEL_LINE   *Lines,
  IN UINTN               Count,
  IN OPENABL_STORAGE_LOG Log OPTIONAL
)
{
  TLMM_PROTOCOL *Tlmm = NULL;
  SPMI_PROTOCOL *Spmi = NULL;
  CHAR8          Scratch[32];

  if (!Count) {
    Say (Log, "panel lines: %s: nothing to switch for this model", What);
    return;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    PANEL_LINE            Line = Lines[Index];
    PANEL_LINE_SPMI_WRITE Writes[4];
    CONST CHAR8          *Kind;
    UINTN                 WriteCount;

    if (Line.Kind == PANEL_LINE_TLMM) {
      UINT32     Config;
      EFI_STATUS Status;

      if (!Tlmm) {
        Tlmm = LocateProtocol (&mTlmmGuid);
      }
      if (!Tlmm) {
        Say (
          Log, "panel lines: %s: TLMM GPIO %u: no TLMM protocol", What,
          (UINT32)Line.Gpio
        );
        continue;
      }
      Config = PanelLinesTlmmOutput (Line.Gpio);
      Status = Tlmm->ConfigGpio (Config, TLMM_ENABLE);
      if (Status == EFI_SUCCESS) {
        Status = Tlmm->GpioOut (Config, GPIO_LOW);
      }
      Say (
        Log, "panel lines: %s: TLMM GPIO %u low: %s", What, (UINT32)Line.Gpio,
        StatusString (Status, Scratch)
      );
      continue;
    }
    Kind = Line.Kind == PANEL_LINE_PMIC_GPIO ? "PMIC GPIO low"
           : Line.Kind == PANEL_LINE_PWM     ? "PWM off"
                                             : "WLED off";
    if (!Spmi) {
      Spmi = LocateProtocol (&mSpmiGuid);
      if (Spmi) {
        Say (
          Log, "panel lines: SPMI protocol revision 0x%05llx%s",
          (UINT64)Spmi->Revision,
          Spmi->Revision >= SPMI_REVISION_BUS_ID ? " (bus id first)"
                                                 : " (no bus id)"
        );
      }
    }
    WriteCount = PanelLineSpmiOff (Line, Writes);
    for (UINTN Index2 = 0; Index2 < WriteCount; Index2++) {
      CHAR8  Prefix[LINE_SIZE];
      UINT32 Result;

      SPrint (
        Prefix, sizeof (Prefix),
        "panel lines: %s: %s: SPMI %u:0x%04x <- 0x%02x", What, Kind,
        (UINT32)Writes[Index2].Sid, (UINT32)Writes[Index2].Addr,
        (UINT32)Writes[Index2].Value
      );
      if (!Spmi) {
        Say (Log, "%s: no SPMI protocol", Prefix);
        break;
      }
      Result = SpmiWrite (
        Spmi, Writes[Index2].Sid, Writes[Index2].Addr, Writes[Index2].Value
      );
      if (Result == SPMI_SUCCESS) {
        Say (Log, "%s: ok", Prefix);
      } else {
        Say (Log, "%s: failed (%u)", Prefix, (UINT32)Result);
      }
    }
  }
}

VOID
DisplayHandoverBacklightOff (IN OPENABL_STORAGE_LOG Log OPTIONAL)
{
  CONST PANEL_LINES *Lines = LinesForModel (Log);

  if (Lines) {
    DriveOff ("backlight", Lines->Backlight, Lines->BacklightCount, Log);
  }
}

VOID
DisplayHandoverSuppliesOff (IN OPENABL_STORAGE_LOG Log OPTIONAL)
{
  CONST PANEL_LINES *Lines = LinesForModel (Log);

  if (Lines) {
    DriveOff ("supplies", Lines->Supplies, Lines->SupplyCount, Log);
  }
}

#endif // OABL_DISPLAY_QUIESCE

// Set when Efi/Splash declares the splash in the kernel's devicetree.
STATIC BOOLEAN mSplashWasDeclared;

VOID
DisplayHandoverSplashDeclared (VOID)
{
  mSplashWasDeclared = TRUE;
}

VOID
DisplayHandoverBeforeExit (
  IN BOOLEAN             KeepAlive,
  IN OPENABL_STORAGE_LOG Log OPTIONAL
)
{
  // The setting overrides the ABL's KeepAlive. Linux devicetrees can have
  // a splash region too, which the ABL takes for Android's, and a requested
  // display-off must happen.
  UINT8 Quiesce = DisplayHandoverQuiesceSetting ();

  KeepAlive =
    mSplashWasDeclared ||
    (KeepAlive &&
     (Quiesce & (DISPLAY_HANDOVER_QUIESCE | DISPLAY_HANDOVER_LINES_OFF)) == 0);
  if (KeepAlive) {
    Say (Log, "the devicetree keeps the splash: the display stays on");
    return;
  }
  // Backlight first: while it is on, the gap between XBL stopping DSI and
  // turning the backlight off shows as a flash on an LCD, and so does the
  // kernel's panel reset later. The supplies go last because the display
  // power-off leaves them on.
#ifdef OABL_DISPLAY_QUIESCE
  if (Quiesce & DISPLAY_HANDOVER_LINES_OFF) {
    DisplayHandoverBacklightOff (Log);
  }
  if (Quiesce & DISPLAY_HANDOVER_QUIESCE) {
    DisplayHandoverQuiesce (Log);
  }
  if (Quiesce & DISPLAY_HANDOVER_LINES_OFF) {
    DisplayHandoverSuppliesOff (Log);
  }
#endif
}
