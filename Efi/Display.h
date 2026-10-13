/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Display - draws frames through the Graphics Output Protocol.
*/

#ifndef __EFI_DISPLAY_H__
#define __EFI_DISPLAY_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

// A graphics output and the size of its current mode. It is opened with
// EFI_OPEN_PROTOCOL_GET_PROTOCOL so the console driver stays bound to it.
// There is nothing to close.
typedef struct _DISPLAY {
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  UINTN                         W, H;
} DISPLAY;

// Opens the first graphics output in its current mode. On handhelds that is
// the panel's native resolution, and changing it isn't supported.
EFI_STATUS
DisplayOpen (OUT DISPLAY *Out);

// Opens the graphics output whose framebuffer is in XBL's display region,
// or else the first one that opens. Returns EFI_NOT_FOUND if none opens.
EFI_STATUS
DisplayOpenPanel (OUT DISPLAY *Out);

STATIC inline VOID
DisplaySize (
  IN CONST DISPLAY *Display,
  OUT UINTN        *W,
  OUT UINTN        *H
)
{
  *W = Display->W;
  *H = Display->H;
}

// Copies a W x H rectangle of 0x00RRGGBB pixels, Stride pixels per row, to
// (X, Y) on the screen. The arguments match the Gfx presenter's PRESENT_BLIT.
// A rectangle that doesn't fit the mode is skipped.
VOID
DisplayBlit (
  IN DISPLAY      *Display,
  IN CONST UINT32 *Pixels,
  IN UINT32        Stride,
  IN UINT32        X,
  IN UINT32        Y,
  IN UINT32        W,
  IN UINT32        H
);

#endif
