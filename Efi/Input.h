/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Input - maps firmware key events to UI inputs.

  Up and volume up are Up, down and volume down are Down, Esc and Backspace
  are Back, and Enter, Space and SCAN_SUSPEND (the power key on Qualcomm
  keypads) are Select. Other keys are ignored. Repeat and hold timing is
  left to the UI's key decoder (Ui/Ui.h).
*/

#ifndef __EFI_INPUT_H__
#define __EFI_INPUT_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Ui/Ui.h"

typedef struct _INPUT_KEYS {
  KEYS Decoder;
} INPUT_KEYS;

typedef struct _INPUT_EVENTS OABL_VEC (INPUT) INPUT_EVENTS;

STATIC inline INPUT_KEYS
InputKeysNew (VOID)
{
  return (INPUT_KEYS){0};
}

// Returns TRUE if a key was already down at startup, as when volume down is
// held for the menu. Reads one mapped key.
BOOLEAN
InputKeysHeldAtStart (IN INPUT_KEYS *Keys);

// Feeds every waiting key through the decoder at time Now (ms) and appends
// the resulting inputs to Out. Caller frees Out with VecFree.
VOID
InputKeysPoll (
  IN OUT INPUT_KEYS   *Keys,
  IN UINT64            Now,
  IN OUT INPUT_EVENTS *Out
);

#endif
