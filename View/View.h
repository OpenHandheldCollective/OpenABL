/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  View - the menu's screens, laid out with microui.

  Sizes are in layout units (the short edge / 360), so every panel gets the
  same proportions. Landscape uses a two-pane menu and portrait stacks the
  panes.
*/

#ifndef __VIEW_VIEW_H__
#define __VIEW_VIEW_H__

#include "Base/OablBase.h"

#include "Gfx/Gfx.h"
#include "Ui/Ui.h"

typedef struct _PANEL {
  UINT32       W, H;    // physical pixels
  PRESENT_BLIT Present; // rotated for the panel
  UINT32 (*Now) (VOID); // milliseconds
  // Simulator only, NULL otherwise. Gets a copy of each frame before
  // rotation, at the logical size.
  UINT32 *Logical;
} PANEL;

// Sets up the view to draw on Panel with the font from Assets. Fails when
// memory runs out or the font doesn't load.
BOOLEAN
ViewInit (
  IN CONST PANEL *Panel,
  IN CONST UINT8 *Font,
  IN UINTN        FontLength
);

// Rotates the picture Degrees clockwise (from AppDesiredRotation).
VOID
ViewRotate (IN UINT16 Degrees);

// Returns the picture's size after rotation.
VOID
ViewLogicalSize (
  OUT UINT32 *W,
  OUT UINT32 *H
);

// Updates the screen to match the app and clears A->Dirty.
VOID
ViewSync (IN OUT APP *A);

// Runs the animations and redraws that are due.
VOID
ViewRun (VOID);

// Finishes the animations and draws now, for the simulator's stills.
VOID
ViewSettle (VOID);

// Frees everything. The simulator calls it between panels.
VOID
ViewDone (VOID);

// Draws what is pending now, or with Everything the whole screen. The
// simulator uses it to check that redrawing only the changed areas gives
// the same picture as a full redraw.
VOID
ViewRedraw (IN BOOLEAN Everything);

#endif
