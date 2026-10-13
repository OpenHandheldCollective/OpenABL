/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Frame - microui's command list drawn onto a canvas and put on the panel.

  The picture is split into square cells. Each command is hashed into the
  cells it can touch, and a frame redraws only the cells whose hash
  changed. The area they cover goes to the panel in one blit, rotated.
*/

#ifndef __VIEW_FRAME_H__
#define __VIEW_FRAME_H__

#include "Base/OablBase.h"

#include "microui.h"
#include "View/View.h"

// Allocates the canvas and microui's context for Panel.
BOOLEAN
FrameInit (IN CONST PANEL *Panel);

VOID
FrameDone (VOID);

// Turns the picture Degrees clockwise. The next draw repaints everything.
VOID
FrameRotate (IN UINT16 Degrees);

// The picture's size after rotation.
VOID
FrameSize (
  OUT INT32 *W,
  OUT INT32 *H
);

// Makes the next draw repaint everything, as after the masks were freed.
VOID
FrameInvalidate (VOID);

// Starts a command list. Draw masks with FrameMaskId.
mu_Context *
FrameBegin (VOID);

// Finishes the command list. The next FrameDraw draws it.
VOID
FrameEnd (VOID);

// Returns the mu_draw_icon id that draws Mask, or -1 when the frame has no
// room for more.
INT32
FrameMaskId (IN CONST MASK *Mask);

// Draws the command list if it hasn't been drawn, redrawing only what
// changed. With Everything it repaints and presents the whole picture.
VOID
FrameDraw (IN BOOLEAN Everything);

#endif
