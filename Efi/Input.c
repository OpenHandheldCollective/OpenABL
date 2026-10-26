/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Input - maps firmware key events to UI inputs.
*/

#include "Efi/Input.h"

#include "Efi/Efi.h"

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
  INPUT   Key;
  INPUT   Decoded;
  BOOLEAN Any = FALSE;

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
}
