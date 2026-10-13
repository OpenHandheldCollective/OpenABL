/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Text - the menu font laid out: advances in whole pixels, no kerning, word
  wrap and an ellipsis for text that doesn't fit.
*/

#ifndef __VIEW_TEXT_H__
#define __VIEW_TEXT_H__

#include "Base/OablBase.h"

#include "Gfx/Gfx.h"

// One size and weight. microui's mu_Font points at one of these.
typedef struct _TEXT_FONT {
  float       Px;
  FONT_WEIGHT Weight;
  INT32       LineH; // a line's height
  INT32       Base;  // from the baseline to the line's bottom
} TEXT_FONT;

// A line of wrapped text, Length bytes from Start.
typedef struct _TEXT_SPAN {
  UINT32 Start;
  UINT32 Length;
} TEXT_SPAN;

// Returns the font at Px pixels per em. The fonts are kept until
// TextFontsReset; past the limit the nearest size is used.
CONST TEXT_FONT *
TextFont (
  IN float       Px,
  IN FONT_WEIGHT Weight
);

// Forgets the fonts. No command list may still point at them.
VOID
TextFontsReset (VOID);

// Returns the width of the first Length bytes of String (MAX_UINTN for
// all of it), not counting spaces at the end.
INT32
TextWidth (
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN UINTN            Length
);

// Copies String to Out, shortened with an ellipsis to fit MaxW.
VOID
TextFit (
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN INT32            MaxW,
  OUT CHAR8          *Out,
  IN UINTN            Cap
);

// Breaks String into lines MaxW wide. Returns how many lines it takes and
// fills in the first Max of them. An empty string takes none.
UINTN
TextWrap (
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN INT32            MaxW,
  OUT TEXT_SPAN      *Lines OPTIONAL,
  IN UINTN            Max
);

// Returns where the "..." goes when String takes more than MaxLines lines
// of MaxW: under MaxW less the dots' width on the last line, which breaks
// anywhere. The text from there on is replaced.
UINTN
TextDotsAt (
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN INT32            MaxW,
  IN UINTN            MaxLines
);

// Gets the box the glyphs of the first Length bytes of String cover
// (MAX_UINTN for all), with the line's top left at (X, Top). Returns FALSE
// when nothing is drawn.
BOOLEAN
TextBounds (
  IN CONST TEXT_FONT *Font,
  IN INT32            X,
  IN INT32            Top,
  IN CONST CHAR8     *String,
  IN UINTN            Length,
  OUT INT32          *X0,
  OUT INT32          *Y0,
  OUT INT32          *X1,
  OUT INT32          *Y1
);

#endif
