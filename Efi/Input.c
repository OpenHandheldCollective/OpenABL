/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Input - maps firmware key events and Moorechip handheld D-Pad UART events to
  UI inputs.
*/

#include "Efi/Input.h"

#include "Base/Memory.h"
#include "Base/Text.h"
#include "Efi/Efi.h"
#include "Efi/Qcom.h"

// EFI_TLMM_PROTOCOL_GUID (DALTLMM.dll): used to configure TLMM UART pins and
// MCU enable/reset GPIOs for Moorechip handheld UART gamepads.
STATIC EFI_GUID mTlmmGuid = {0xad9aec18,
                             0x7bf0,
                             0x4809,
                             {0x9e, 0x96, 0x30, 0x12, 0x30, 0x9f, 0x3d, 0xf7}};

typedef struct _TLMM_PROTOCOL {
  UINT64 Revision;
  EFI_STATUS (EFIAPI *ConfigGpio) (
    IN UINT32 Config,
    IN UINT32 Enable
  );
  VOID *ConfigGpioGroup;
  EFI_STATUS (EFIAPI *GpioIn) (
    IN UINT32   Config,
    OUT UINT32 *Value
  );
  EFI_STATUS (EFIAPI *GpioOut) (
    IN UINT32 Config,
    IN UINT32 Value
  );
} TLMM_PROTOCOL;

#define TLMM_GPIO_ENABLE  1U
#define TLMM_GPIO_INPUT   0U
#define TLMM_GPIO_OUTPUT  1U
#define TLMM_GPIO_NO_PULL 0U
#define TLMM_GPIO_PULL_UP 3U
#define TLMM_GPIO_2MA     0U
#define TLMM_GPIO_LOW     0U
#define TLMM_GPIO_HIGH    1U

STATIC UINT32
TlmmCfg (
  IN UINT16 Gpio,
  IN UINT8  Func,
  IN UINT8  Dir,
  IN UINT8  Pull,
  IN UINT8  Drv
)
{
  return ((UINT32)(Func & 0xFU)) | (((UINT32)(Gpio & 0x3FFU)) << 4) |
         (((UINT32)(Dir & 0x1U)) << 14) | (((UINT32)(Pull & 0x3U)) << 15) |
         (((UINT32)(Drv & 0x7U)) << 17);
}

STATIC BOOLEAN        mTlmmLookedUp;
STATIC TLMM_PROTOCOL *mTlmm;

STATIC VOID
EnsureTlmm (VOID)
{
  EFI_HANDLE *Handles = NULL;
  UINTN       Count;

  if (mTlmmLookedUp) {
    return;
  }
  mTlmmLookedUp = TRUE;
  Count         = LocateHandles (&mTlmmGuid, &Handles);
  if (Count) {
    mTlmm = GetProtocol (Handles[0], &mTlmmGuid);
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
}

// Moorechip QUPv3 GENI UART handheld gamepad (configured from devicetree)

typedef struct _PAD_CLOCK {
  UINT64 VoteReg;
  UINT32 VoteBits;
  UINT64 RcgBase;
  UINT8  UartFunc;
} PAD_CLOCK;

STATIC inline UINT32
RegRead32 (IN UINT64 Addr)
{
  return *(volatile CONST UINT32 *)(UINTN)Addr;
}

STATIC inline VOID
RegWrite32 (
  IN UINT64 Addr,
  IN UINT32 Val
)
{
  *(volatile UINT32 *)(UINTN)Addr = Val;
}

STATIC VOID
PadGpioOut (
  IN UINT16 Gpio,
  IN UINT32 Value
)
{
  UINT32 Cfg;

  if (!mTlmm || Gpio == DT_NO_GPIO) {
    return;
  }
  Cfg = TlmmCfg (Gpio, 0, TLMM_GPIO_OUTPUT, TLMM_GPIO_NO_PULL, TLMM_GPIO_2MA);
  (VOID)mTlmm->ConfigGpio (Cfg, TLMM_GPIO_ENABLE);
  (VOID)mTlmm->GpioOut (Cfg, Value);
}

// Validates that the devicetree's UART base and TX/RX pins match a known QUPv3
// serial engine on the running SoC before touching any GCC clock or TLMM
// register.
STATIC BOOLEAN
GamepadClockOf (
  IN CONST DT_GAMEPAD *Pad,
  OUT PAD_CLOCK       *Out
)
{
  QCOM_CHIP Chip;
  UINT32    Id;
  OABL_TEXT Name;

  if (!QcomChip (&Chip)) {
    return FALSE;
  }
  Id   = EFI_ERROR (Chip.Id.Status) ? 0 : Chip.Id.Value;
  Name = TextTrim (TextOf (Chip.Name));

  // SM8250 QUPv3_2 SE2 (uart16 @ 0x00888000, gpio50/51 func 1).
  if ((Id == 356 || TextStartsWith (Name, "SM8250")) &&
      Pad->UartBase == 0x00888000ULL && Pad->TxGpio == 50 &&
      Pad->RxGpio == 51) {
    *Out = (PAD_CLOCK){
      .VoteReg  = 0x00152010ULL,
      .VoteBits = 0x0000004FU,
      .RcgBase  = 0x0011E270ULL,
      .UartFunc = 1,
    };
    return TRUE;
  }

  // SM8550 / QCS8550 QUPv3_2 SE7 (uart15 @ 0x0089C000, gpio74/75 func 2).
  if ((Id == 601 || Id == 603 || Id == 519 || Id == 536 ||
       TextStartsWith (Name, "SM8550") || TextStartsWith (Name, "QCS8550")) &&
      Pad->UartBase == 0x0089C000ULL && Pad->TxGpio == 74 &&
      Pad->RxGpio == 75) {
    *Out = (PAD_CLOCK){
      .VoteReg  = 0x00152010ULL,
      .VoteBits = 0x0002000FU,
      .RcgBase  = 0x0011E898ULL,
      .UartFunc = 2,
    };
    return TRUE;
  }

  // SM8750 / CQ8725S QUPv3_2 SE7 (uart15 @ 0x0089C000, gpio30/31 func 1).
  if ((Id == 706 || Id == 639 || Id == 618 ||
       TextStartsWith (Name, "CQ8725S") || TextStartsWith (Name, "SM8750")) &&
      Pad->UartBase == 0x0089C000ULL && Pad->TxGpio == 30 &&
      Pad->RxGpio == 31) {
    *Out = (PAD_CLOCK){
      .VoteReg  = 0x00152010ULL,
      .VoteBits = 0x0002000FU,
      .RcgBase  = 0x0011E8B8ULL,
      .UartFunc = 1,
    };
    return TRUE;
  }

  return FALSE;
}

STATIC VOID
GeniUartStop (IN UINT64 UartBase)
{
  UINT32 FifoWords;

  if (!UartBase) {
    return;
  }
  RegWrite32 (UartBase + 0x614, 0);
  RegWrite32 (UartBase + 0x644, 0);
  if (RegRead32 (UartBase + 0x040) & 0x1000U) {
    RegWrite32 (UartBase + 0x634, 0x4U);
    gBS->Stall (200);
    if (RegRead32 (UartBase + 0x040) & 0x1000U) {
      RegWrite32 (UartBase + 0x634, 0x2U);
      gBS->Stall (200);
    }
  }
  if (RegRead32 (UartBase + 0x040) & 0x1U) {
    RegWrite32 (UartBase + 0x604, 0x4U);
    gBS->Stall (200);
    if (RegRead32 (UartBase + 0x040) & 0x1U) {
      RegWrite32 (UartBase + 0x604, 0x2U);
      gBS->Stall (200);
    }
  }
  FifoWords = RegRead32 (UartBase + 0x804) & 0x3FU;
  while (FifoWords--) {
    (VOID)RegRead32 (UartBase + 0x780);
  }
  RegWrite32 (UartBase + 0x618, 0xFFFFFFFFU);
  RegWrite32 (UartBase + 0x648, 0xFFFFFFFFU);
}

STATIC BOOLEAN
GeniUartInit (
  IN CONST DT_GAMEPAD *Pad,
  IN CONST PAD_CLOCK  *Clock
)
{
  UINT64 Uart = Pad->UartBase;
  UINT32 FwRev;

  if (!mTlmm) {
    return FALSE;
  }

  // Enable QUPv3 wrapper + serial engine branch clocks in GCC.
  RegWrite32 (Clock->VoteReg, RegRead32 (Clock->VoteReg) | Clock->VoteBits);
  gBS->Stall (100);

  // Configure RCG for 7,372,800 Hz (115200 * 64) from GPLL0_OUT_EVEN (300 MHz).
  RegWrite32 (Clock->RcgBase + 0x04, 0x00002601U);
  RegWrite32 (Clock->RcgBase + 0x08, 0x00000180U);
  RegWrite32 (Clock->RcgBase + 0x0C, 0x0000C476U);
  RegWrite32 (Clock->RcgBase + 0x10, 0x0000C2F6U);
  RegWrite32 (Clock->RcgBase + 0x00, RegRead32 (Clock->RcgBase + 0x00) | 0x1U);
  for (UINTN Spin = 0; Spin < 100; Spin++) {
    if ((RegRead32 (Clock->RcgBase + 0x00) & 0x1U) == 0) {
      break;
    }
    gBS->Stall (10);
  }

  // Verify TZ loaded GENI_SE_UART (protocol 2) into this serial engine.
  FwRev = RegRead32 (Uart + 0x068);
  if (((FwRev >> 8) & 0xFFU) != 2U) {
    return FALSE;
  }

  // Route TX and RX pins to the QUPv3 serial engine in TLMM.
  (VOID)mTlmm->ConfigGpio (
    TlmmCfg (
      Pad->TxGpio, Clock->UartFunc, TLMM_GPIO_OUTPUT, TLMM_GPIO_PULL_UP,
      TLMM_GPIO_2MA
    ),
    TLMM_GPIO_ENABLE
  );
  (VOID)mTlmm->ConfigGpio (
    TlmmCfg (
      Pad->RxGpio, Clock->UartFunc, TLMM_GPIO_INPUT, TLMM_GPIO_PULL_UP,
      TLMM_GPIO_2MA
    ),
    TLMM_GPIO_ENABLE
  );

  GeniUartStop (Uart);

  // Configure FIFO mode, 4x8-bit byte packing, and 115200 8N1 (div=4, 16x).
  RegWrite32 (Uart + 0x000, 0x0U);
  RegWrite32 (Uart + 0x028, RegRead32 (Uart + 0x028) | 0x7FU);
  RegWrite32 (Uart + 0x020, 0x1U);
  RegWrite32 (Uart + 0x024, 0x7FU);
  RegWrite32 (Uart + 0x258, 0x0U);
  RegWrite32 (Uart + 0xE18, 0x0U);
  RegWrite32 (Uart + 0x260, 0x0004380EU);
  RegWrite32 (Uart + 0x264, 0x000C3E0EU);
  RegWrite32 (Uart + 0x284, 0x0004380EU);
  RegWrite32 (Uart + 0x288, 0x000C3E0EU);
  RegWrite32 (Uart + 0x07C, 0x0U);
  RegWrite32 (Uart + 0x048, 0x41U);
  RegWrite32 (Uart + 0x04C, 0x41U);
  RegWrite32 (Uart + 0x25C, 0x2U);
  RegWrite32 (Uart + 0x268, 0x8U);
  RegWrite32 (Uart + 0x26C, 0x0U);
  RegWrite32 (Uart + 0x280, 0x0U);
  RegWrite32 (Uart + 0x28C, 0x8U);
  RegWrite32 (Uart + 0x294, 0xA0U);
  RegWrite32 (Uart + 0x2A4, 0x0U);
  RegWrite32 (Uart + 0x2A8, 0x0U);
  RegWrite32 (Uart + 0x618, 0xFFFFFFFFU);
  RegWrite32 (Uart + 0x648, 0xFFFFFFFFU);

  // Start secondary engine RX (UART_START_READ = 1 << 27).
  RegWrite32 (Uart + 0x630, 0x08000000U);
  return TRUE;
}

STATIC VOID
GeniUartTx (
  IN UINT64       Uart,
  IN CONST UINT8 *Data,
  IN UINTN        Len
)
{
  if (!Uart || !Len) {
    return;
  }
  RegWrite32 (Uart + 0x618, 0xFFFFFFFFU);
  RegWrite32 (Uart + 0x270, (UINT32)Len);
  RegWrite32 (Uart + 0x600, 0x08000000U);
  for (UINTN Index = 0; Index < Len; Index += 4) {
    UINT32 Word  = 0;
    UINTN  Chunk = (Len - Index >= 4) ? 4 : (Len - Index);
    for (UINTN Byte = 0; Byte < Chunk; Byte++) {
      Word |= ((UINT32)Data[Index + Byte]) << (8 * Byte);
    }
    RegWrite32 (Uart + 0x700, Word);
  }
  for (UINTN Spin = 0; Spin < 100; Spin++) {
    if (RegRead32 (Uart + 0x610) & 0x1U) {
      break;
    }
    gBS->Stall (100);
  }
  RegWrite32 (Uart + 0x618, 0xFFFFFFFFU);
}

STATIC VOID
PadSendCmd (
  IN OUT INPUT_KEYS *Keys,
  IN UINT8           Cmd,
  IN CONST UINT8    *Payload,
  IN UINTN           PayloadLen
)
{
  UINT8 Frame[32];
  UINT8 Checksum = 0;
  UINTN Len      = 0;

  if (PayloadLen > sizeof (Frame) - 9) {
    return;
  }
  Frame[Len++] = 0xA5;
  Frame[Len++] = 0xD3;
  Frame[Len++] = 0x5A;
  Frame[Len++] = 0x3D;
  Frame[Len++] = Keys->PadSeq++;
  Frame[Len++] = Cmd;
  Frame[Len++] = (UINT8)(PayloadLen & 0xFFU);
  Frame[Len++] = (UINT8)((PayloadLen >> 8) & 0xFFU);
  for (UINTN Index = 0; Index < PayloadLen; Index++) {
    Frame[Len++] = Payload[Index];
  }
  for (UINTN Index = 4; Index < Len; Index++) {
    Checksum ^= Frame[Index];
  }
  Frame[Len++] = Checksum;
  GeniUartTx (Keys->PadConfig.UartBase, Frame, Len);
}

STATIC VOID
GeniUartDrainRx (IN OUT INPUT_KEYS *Keys)
{
  UINT64 Uart = Keys->PadConfig.UartBase;
  UINT32 SStatus;
  UINT32 FifoStatus;
  UINT32 WordCnt;

  if (!Uart) {
    return;
  }
  for (UINTN Pass = 0; Pass < 8; Pass++) {
    SStatus    = RegRead32 (Uart + 0x640);
    FifoStatus = RegRead32 (Uart + 0x804);
    WordCnt    = FifoStatus & 0x01FFFFFFU;
    if (WordCnt > 0) {
      BOOLEAN LastPartial = (FifoStatus & 0x80000000U) != 0;
      UINT32  LastBytes   = (FifoStatus >> 28) & 0x7U;
      UINT32  TotalBytes  = 4U * (WordCnt - 1);

      if (LastPartial && LastBytes) {
        TotalBytes += LastBytes;
      } else {
        TotalBytes += 4U;
      }
      for (UINT32 WordIdx = 0; WordIdx < WordCnt; WordIdx++) {
        UINT32 Word  = RegRead32 (Uart + 0x780);
        UINT32 Chunk = (TotalBytes >= 4U) ? 4U : TotalBytes;
        TotalBytes -= Chunk;
        for (UINT32 Byte = 0; Byte < Chunk; Byte++) {
          UINT8 Val = (UINT8)((Word >> (8 * Byte)) & 0xFFU);
          if (Keys->PadRxLen >= sizeof (Keys->PadRxBuf)) {
            CopyMem (
              Keys->PadRxBuf, Keys->PadRxBuf + 1, sizeof (Keys->PadRxBuf) - 1
            );
            Keys->PadRxLen = sizeof (Keys->PadRxBuf) - 1;
          }
          Keys->PadRxBuf[Keys->PadRxLen++] = Val;
        }
      }
    }
    if (SStatus) {
      RegWrite32 (Uart + 0x648, SStatus);
    }
    if (WordCnt == 0) {
      break;
    }
  }
}

STATIC VOID
PadDropRxBytes (
  IN OUT INPUT_KEYS *Keys,
  IN UINTN           Count
)
{
  if (Count >= Keys->PadRxLen) {
    Keys->PadRxLen = 0;
    return;
  }
  CopyMem (Keys->PadRxBuf, Keys->PadRxBuf + Count, Keys->PadRxLen - Count);
  Keys->PadRxLen -= Count;
}

STATIC VOID
PadUpdateStatus (
  IN OUT INPUT_KEYS *Keys,
  IN CONST UINT8    *Frame,
  IN UINT16          PayloadLen
)
{
  UINT16 Btn;

  if (PayloadLen < 6) {
    return;
  }
  Btn = (UINT16)Frame[8] | ((UINT16)Frame[9] << 8);

  // Only map the digital D-Pad directions (BIT(0)..BIT(3)), which are
  // identical across all Retroid and AYN handhelds. Face buttons and analog
  // sticks are intentionally ignored.
  // [0] = INPUT_UP    (D-Pad Up,    BIT(0))
  // [1] = INPUT_DOWN  (D-Pad Down,  BIT(1))
  // [2] = INPUT_LEFT  (D-Pad Left,  BIT(2))
  // [3] = INPUT_RIGHT (D-Pad Right, BIT(3))
  Keys->PadLiveHeld[0] = (Btn & (1U << 0)) != 0;
  Keys->PadLiveHeld[1] = (Btn & (1U << 1)) != 0;
  Keys->PadLiveHeld[2] = (Btn & (1U << 2)) != 0;
  Keys->PadLiveHeld[3] = (Btn & (1U << 3)) != 0;

  if (!Keys->PadFirstFrameSeen) {
    Keys->PadFirstFrameSeen = TRUE;
    for (UINTN Index = 0; Index < 4; Index++) {
      Keys->PadBtn[Index].HeldAtStart = Keys->PadLiveHeld[Index];
      Keys->PadBtn[Index].PrevHeld    = Keys->PadLiveHeld[Index];
    }
  }
}

STATIC VOID
GamepadParseRx (
  IN OUT INPUT_KEYS *Keys,
  OUT BOOLEAN       *SawVersion
)
{
  *SawVersion = FALSE;
  while (Keys->PadRxLen >= 9) {
    UINT16 PayloadLen;
    UINTN  FrameLen;
    UINT8  Checksum = 0;

    if (Keys->PadRxBuf[0] != 0xA5 || Keys->PadRxBuf[1] != 0xD3 ||
        Keys->PadRxBuf[2] != 0x5A || Keys->PadRxBuf[3] != 0x3D) {
      PadDropRxBytes (Keys, 1);
      continue;
    }
    PayloadLen = (UINT16)Keys->PadRxBuf[6] | ((UINT16)Keys->PadRxBuf[7] << 8);
    if (PayloadLen > 64) {
      PadDropRxBytes (Keys, 4);
      continue;
    }
    FrameLen = 9U + PayloadLen;
    if (Keys->PadRxLen < FrameLen) {
      break;
    }
    for (UINTN Index = 4; Index < FrameLen - 1; Index++) {
      Checksum ^= Keys->PadRxBuf[Index];
    }
    if (Checksum != Keys->PadRxBuf[FrameLen - 1]) {
      PadDropRxBytes (Keys, 4);
      continue;
    }
    if (Keys->PadRxBuf[5] == 0x01 && PayloadLen >= 1 &&
        Keys->PadRxBuf[8] == 0x02) {
      *SawVersion = TRUE;
    } else if (Keys->PadRxBuf[5] == 0x02) {
      Keys->PadActive = TRUE;
      PadUpdateStatus (Keys, Keys->PadRxBuf, PayloadLen);
    }
    PadDropRxBytes (Keys, FrameLen);
  }
}

STATIC VOID
PollGamepad (
  IN OUT INPUT_KEYS *Keys,
  IN UINT64          Now,
  OUT BOOLEAN        Held[4]
)
{
  STATIC CONST UINT8 Cmd0701[]  = {0x07, 0x01};
  STATIC CONST UINT8 Cmd06[]    = {0x06};
  STATIC CONST UINT8 CmdVer[]   = {0x02};
  STATIC CONST UINT8 CmdPar[]   = {0x05, 0x01, 0x00, 0x00, 0x00,
                                   0x28, 0x00, 0x00, 0x00, 0x07};
  BOOLEAN            SawVersion = FALSE;

  for (UINTN Index = 0; Index < 4; Index++) {
    Held[Index] = FALSE;
  }

  if (!Keys->PadConfigured) {
    return;
  }

  if (!Keys->PadInitDone) {
    PAD_CLOCK Clock;

    EnsureTlmm ();
    Keys->PadInitDone = TRUE;
    if (!GamepadClockOf (&Keys->PadConfig, &Clock) ||
        !GeniUartInit (&Keys->PadConfig, &Clock)) {
      Keys->PadStep = 99;
      return;
    }
    PadGpioOut (Keys->PadConfig.VddGpio, TLMM_GPIO_HIGH);
    PadGpioOut (Keys->PadConfig.BootGpio, TLMM_GPIO_LOW);
    PadGpioOut (Keys->PadConfig.EnGpio, TLMM_GPIO_LOW);
    PadGpioOut (Keys->PadConfig.RstGpio, TLMM_GPIO_LOW);
    Keys->PadStep       = 1;
    Keys->PadNextMs     = Now + 80;
    Keys->PadLastPollMs = Now;
    return;
  }

  if (Keys->PadStep == 99 || !Keys->PadConfig.UartBase) {
    return;
  }

  // If a synchronous command (such as scanning filesystems in FillEntries)
  // blocked the main loop for > 80 ms, the 64-byte hardware RX FIFO may have
  // filled and truncated a status frame. Drain and discard stale bytes so the
  // next fresh MCU status frame parses cleanly without re-triggering.
  if (Keys->PadActive && Keys->PadLastPollMs != 0 &&
      Now > Keys->PadLastPollMs && Now - Keys->PadLastPollMs > 80) {
    GeniUartDrainRx (Keys);
    Keys->PadRxLen = 0;
  }
  Keys->PadLastPollMs = Now;

  GeniUartDrainRx (Keys);
  GamepadParseRx (Keys, &SawVersion);

  switch (Keys->PadStep) {
  case 1:
    if (Now >= Keys->PadNextMs) {
      PadGpioOut (Keys->PadConfig.EnGpio, TLMM_GPIO_HIGH);
      PadGpioOut (Keys->PadConfig.RstGpio, TLMM_GPIO_HIGH);
      Keys->PadRxLen  = 0;
      Keys->PadStep   = Keys->PadConfig.IsRetroid8250 ? 10 : 2;
      Keys->PadNextMs = Now + (Keys->PadConfig.IsRetroid8250 ? 100 : 60);
    }
    break;
  case 2:
    if (Now >= Keys->PadNextMs) {
      Keys->PadRxLen = 0;
      PadSendCmd (Keys, 0x01, CmdVer, sizeof (CmdVer));
      Keys->PadStep   = 3;
      Keys->PadNextMs = Now + 400;
    }
    break;
  case 3:
    if (SawVersion || Keys->PadActive) {
      PadSendCmd (Keys, 0x01, CmdPar, sizeof (CmdPar));
      Keys->PadActive = TRUE;
      Keys->PadStep   = 20;
    } else if (Now >= Keys->PadNextMs) {
      if (++Keys->PadAttempts < 3) {
        PadGpioOut (Keys->PadConfig.EnGpio, TLMM_GPIO_LOW);
        PadGpioOut (Keys->PadConfig.RstGpio, TLMM_GPIO_LOW);
        Keys->PadStep   = 1;
        Keys->PadNextMs = Now + 80;
      } else {
        Keys->PadStep = 99;
      }
    }
    break;
  case 10:
    if (Now >= Keys->PadNextMs) {
      PadSendCmd (Keys, 0x01, Cmd0701, sizeof (Cmd0701));
      Keys->PadStep   = 11;
      Keys->PadNextMs = Now + 100;
    }
    break;
  case 11:
    if (Now >= Keys->PadNextMs) {
      PadSendCmd (Keys, 0x01, Cmd06, sizeof (Cmd06));
      Keys->PadStep   = 12;
      Keys->PadNextMs = Now + 100;
    }
    break;
  case 12:
    if (Now >= Keys->PadNextMs) {
      PadSendCmd (Keys, 0x01, CmdVer, sizeof (CmdVer));
      Keys->PadStep   = 13;
      Keys->PadNextMs = Now + 100;
    }
    break;
  case 13:
    if (Now >= Keys->PadNextMs) {
      PadSendCmd (Keys, 0x01, CmdPar, sizeof (CmdPar));
      Keys->PadStep   = 14;
      Keys->PadNextMs = Now + 100;
    }
    break;
  case 14:
    if (Now >= Keys->PadNextMs) {
      PadSendCmd (Keys, 0x01, Cmd06, sizeof (Cmd06));
      Keys->PadStep   = 15;
      Keys->PadNextMs = Now + 100;
    }
    break;
  case 15:
    if (Now >= Keys->PadNextMs) {
      PadSendCmd (Keys, 0x01, CmdVer, sizeof (CmdVer));
      Keys->PadActive = TRUE;
      Keys->PadStep   = 20;
    }
    break;
  case 20:
  default:
    break;
  }

  if (Keys->PadActive) {
    for (UINTN Index = 0; Index < 4; Index++) {
      Held[Index] = Keys->PadLiveHeld[Index];
    }
  }
}

VOID
InputKeysClose (IN OUT INPUT_KEYS *Keys)
{
  if (!Keys->PadInitDone || Keys->PadStep == 99 || !Keys->PadConfig.UartBase) {
    return;
  }
  PadGpioOut (Keys->PadConfig.EnGpio, TLMM_GPIO_LOW);
  PadGpioOut (Keys->PadConfig.RstGpio, TLMM_GPIO_LOW);
  gBS->Stall (1000);
  GeniUartStop (Keys->PadConfig.UartBase);
  Keys->PadActive = FALSE;
  Keys->PadStep   = 99;
}

VOID
InputKeysConfigureGamepad (
  IN OUT INPUT_KEYS   *Keys,
  IN CONST DT_GAMEPAD *Pad OPTIONAL
)
{
  if (!Pad) {
    InputKeysClose (Keys);
    Keys->PadConfigured = FALSE;
    Keys->PadConfig     = (DT_GAMEPAD){0};
    Keys->PadInitDone   = FALSE;
    return;
  }
  if (Keys->PadConfigured &&
      CompareMem (&Keys->PadConfig, Pad, sizeof (*Pad)) == 0) {
    return;
  }
  InputKeysClose (Keys);
  Keys->PadConfigured     = TRUE;
  Keys->PadConfig         = *Pad;
  Keys->PadInitDone       = FALSE;
  Keys->PadActive         = FALSE;
  Keys->PadFirstFrameSeen = FALSE;
  Keys->PadStep           = 0;
  Keys->PadAttempts       = 0;
  Keys->PadRxLen          = 0;
  for (UINTN Index = 0; Index < 4; Index++) {
    Keys->PadLiveHeld[Index] = FALSE;
    Keys->PadBtn[Index]      = (LEVEL_BTN){0};
  }
}

// Reads the next key the UI uses, skipping others. Returns FALSE when none
// is waiting, the read fails, or the character is a UTF-16 surrogate.
STATIC BOOLEAN
ReadKey (OUT INPUT *Out)
{
  EFI_INPUT_KEY Key;

  if (!gST->ConIn) {
    return FALSE;
  }
  for (;;) {
    if (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key) != EFI_SUCCESS) {
      return FALSE;
    }
    if (Key.ScanCode != SCAN_NULL) {
      switch (Key.ScanCode) {
      case SCAN_UP:
      case SCAN_VOLUME_UP:
        *Out = INPUT_UP;
        return TRUE;
      case SCAN_DOWN:
      case SCAN_VOLUME_DOWN:
        *Out = INPUT_DOWN;
        return TRUE;
      case SCAN_LEFT:
        *Out = INPUT_LEFT;
        return TRUE;
      case SCAN_RIGHT:
        *Out = INPUT_RIGHT;
        return TRUE;
      case SCAN_ESC:
        *Out = INPUT_BACK;
        return TRUE;
      // Qualcomm keypad drivers report the power key as SCAN_SUSPEND.
      case SCAN_SUSPEND:
        *Out = INPUT_SELECT;
        return TRUE;
      }
      continue;
    }
    if (Key.UnicodeChar >= 0xD800 && Key.UnicodeChar <= 0xDFFF) {
      return FALSE;
    }
    switch (Key.UnicodeChar) {
    case '\r':
    case ' ':
      *Out = INPUT_SELECT;
      return TRUE;
    case 8:
      *Out = INPUT_BACK;
      return TRUE;
    }
  }
}

// Resets the input after a read. Qualcomm's keypad reports a press once;
// after a reset it reports a key still down again, which is how a held key
// is seen. Qualcomm's menu resets after each read for the same reason.
STATIC VOID
ResetKeys (VOID)
{
  (VOID)gST->ConIn->Reset (gST->ConIn, FALSE);
}

#define BTN_RELEASE_DEBOUNCE_MS 80ULL

STATIC BOOLEAN
PollLevelButton (
  IN OUT LEVEL_BTN *Btn,
  IN BOOLEAN        Held,
  IN INPUT          Key,
  IN UINT64         Now,
  OUT INPUT        *Out
)
{
  UINT64 Tick = Now ? Now : 1;

  if (Btn->HeldAtStart) {
    if (!Held) {
      if (Btn->ReleaseMs == 0) {
        Btn->ReleaseMs = Tick;
      } else if (Tick - Btn->ReleaseMs >= BTN_RELEASE_DEBOUNCE_MS) {
        Btn->HeldAtStart = FALSE;
        Btn->PrevHeld    = FALSE;
        Btn->ReleaseMs   = 0;
      }
    } else {
      Btn->ReleaseMs = 0;
    }
    return FALSE;
  }

  if (Held) {
    Btn->ReleaseMs = 0;
    if (!Btn->PrevHeld) {
      Btn->PrevHeld    = TRUE;
      Btn->HoldStartMs = Tick;
      Btn->LastEmitMs  = Tick;
      *Out             = Key;
      return TRUE;
    }
    if ((Key == INPUT_UP || Key == INPUT_DOWN) &&
        Tick - Btn->HoldStartMs >= KEY_REPEAT_DELAY_MS &&
        Tick - Btn->LastEmitMs >= KEY_REPEAT_EVERY_MS) {
      Btn->LastEmitMs = Tick;
      *Out            = Key;
      return TRUE;
    }
    return FALSE;
  }

  if (Btn->PrevHeld) {
    if (Btn->ReleaseMs == 0) {
      Btn->ReleaseMs = Tick;
    } else if (Tick - Btn->ReleaseMs >= BTN_RELEASE_DEBOUNCE_MS) {
      Btn->PrevHeld  = FALSE;
      Btn->ReleaseMs = 0;
    }
  }
  return FALSE;
}

BOOLEAN
InputKeysHeldAtStart (
  IN OUT INPUT_KEYS *Keys,
  IN UINT64          Now
)
{
  INPUT Key;

  if (!ReadKey (&Key)) {
    return FALSE;
  }
  ResetKeys ();
  KeysMute (&Keys->Decoder, Key, Now);
  return TRUE;
}

VOID
InputKeysPoll (
  IN OUT INPUT_KEYS   *Keys,
  IN UINT64            Now,
  IN OUT INPUT_EVENTS *Out
)
{
  STATIC CONST INPUT PadMap[4] = {INPUT_UP, INPUT_DOWN, INPUT_LEFT,
                                  INPUT_RIGHT};
  INPUT              Key;
  INPUT              Decoded;
  BOOLEAN            Pad[4];
  BOOLEAN            Any = FALSE;

  while (ReadKey (&Key)) {
    Any = TRUE;
    // The input is lost if memory runs out.
    if (KeysPress (&Keys->Decoder, Key, Now, &Decoded)) {
      (VOID)VecPush (Out, Decoded);
    }
  }
  if (Any) {
    ResetKeys ();
  } else {
    KeysIdle (&Keys->Decoder, Now);
  }
  PollGamepad (Keys, Now, Pad);
  for (UINTN Index = 0; Index < 4; Index++) {
    if (PollLevelButton (
          &Keys->PadBtn[Index], Pad[Index], PadMap[Index], Now, &Decoded
        )) {
      (VOID)VecPush (Out, Decoded);
    }
  }
}

VOID
InputKeysAfterBusy (
  IN OUT INPUT_KEYS *Keys,
  IN UINT64          Now
)
{
  if (Keys->Decoder.Held) {
    KeysMute (&Keys->Decoder, Keys->Decoder.Key, Now);
  }
  if (Keys->PadActive && Keys->PadConfig.UartBase) {
    GeniUartDrainRx (Keys);
    Keys->PadRxLen      = 0;
    Keys->PadLastPollMs = Now;
  }
}
