/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Input - maps firmware key events and Moorechip handheld D-Pad UART events to
  UI inputs.

  Up and volume up are Up, down and volume down are Down, Esc and Backspace
  are Back, Left/Right and the handheld D-Pad Left/Right are Left/Right, and
  Enter, Space and SCAN_SUSPEND (the power key on Qualcomm keypads) are
  Select. Other keys are ignored. The input is reset after each read, so
  Qualcomm's keypad reports a held key again. Repeat and hold timing is left
  to the UI's key decoder (Ui/Ui.h).
*/

#ifndef __EFI_INPUT_H__
#define __EFI_INPUT_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Core/DeviceTree.h"
#include "Ui/Ui.h"

typedef struct _LEVEL_BTN {
  BOOLEAN HeldAtStart;
  BOOLEAN PrevHeld;
  UINT64  HoldStartMs;
  UINT64  LastEmitMs;
  UINT64  ReleaseMs;
} LEVEL_BTN;

typedef struct _INPUT_KEYS {
  KEYS       Decoder;
  LEVEL_BTN  PadBtn[4];
  BOOLEAN    PadConfigured;
  DT_GAMEPAD PadConfig;
  BOOLEAN    PadFirstFrameSeen;
  BOOLEAN    PadLiveHeld[4];
  BOOLEAN    PadInitDone;
  BOOLEAN    PadActive;
  UINT8      PadStep;
  UINT8      PadAttempts;
  UINT8      PadSeq;
  UINT64     PadNextMs;
  UINT64     PadLastPollMs;
  UINTN      PadRxLen;
  UINT8      PadRxBuf[256];
} INPUT_KEYS;

typedef struct _INPUT_EVENTS OABL_VEC (INPUT) INPUT_EVENTS;

STATIC inline INPUT_KEYS
InputKeysNew (VOID)
{
  return (INPUT_KEYS){0};
}

// Enables or disables handheld gamepad polling using the UART and GPIO
// configuration parsed from the active devicetree. Passing NULL disables
// gamepad polling without touching any hardware registers or GPIOs.
VOID
InputKeysConfigureGamepad (
  IN OUT INPUT_KEYS   *Keys,
  IN CONST DT_GAMEPAD *Pad OPTIONAL
);

// Returns TRUE if a key was already down at startup, as when volume down is
// held for the menu. Reads one mapped key at time Now (ms), which makes no
// input until it is released.
BOOLEAN
InputKeysHeldAtStart (
  IN OUT INPUT_KEYS *Keys,
  IN UINT64          Now
);

// Feeds every waiting key through the decoder at time Now (ms) and appends
// the resulting inputs to Out. Caller frees Out with VecFree.
VOID
InputKeysPoll (
  IN OUT INPUT_KEYS   *Keys,
  IN UINT64            Now,
  IN OUT INPUT_EVENTS *Out
);

// Mutes a key still held after the menu was busy, until it is released, and
// flushes any stale bytes queued in the handheld gamepad UART RX FIFO.
VOID
InputKeysAfterBusy (
  IN OUT INPUT_KEYS *Keys,
  IN UINT64          Now
);

// Quiesces the handheld gamepad MCU and UART before leaving the interactive
// menu so the OS kernel sees an idle serial engine and quiet IRQ line at boot.
VOID
InputKeysClose (IN OUT INPUT_KEYS *Keys);

#endif
