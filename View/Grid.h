/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present PortareOS
 *   https://github.com/portare-ch
 */

/**
  Grid - a text grid on a canvas, drawn the way portarelauncher draws its
  menus: VGA 8x16 glyphs at an integer scale, centred, in a five-step amber
  palette on black. Only the cells that changed since the last flush are
  drawn.
*/

#ifndef __VIEW_GRID_H__
#define __VIEW_GRID_H__

#include "Base/OablBase.h"

// The launcher's ramps: structure DIM, secondary MID, body text TEXT, the
// selected row BRIGHT.
typedef enum {
  ATTR_BG,
  ATTR_DIM,
  ATTR_MID,
  ATTR_TEXT,
  ATTR_BRIGHT,
  ATTR_COUNT
} ATTR;

// CP437 glyphs the menu draws by number.
#define GLYPH_CARET       0x10
#define GLYPH_RULE        0xC4
#define GLYPH_RULE_DOUBLE 0xCD

// The grid the launcher has on a 1280 x 960 panel. A larger picture gets a
// larger scale, so it never shows less than this.
#define GRID_MIN_COLS 53
#define GRID_MIN_ROWS 20

typedef struct _CELL {
  UINT8 Glyph;
  UINT8 Attr;
} CELL;

// A changed area of the canvas: X0 <= x < X1, Y0 <= y < Y1.
typedef struct _GRID_AREA {
  INT32 X0, Y0, X1, Y1;
} GRID_AREA;

typedef struct _GRID {
  UINT32 *Pixels; // W x H
  INT32   W, H;
  UINT32  Scale;
  UINT32  Cols, Rows;
  INT32   Ox, Oy; // the grid's top left on the canvas
  CELL   *Cur;    // what to show
  CELL   *Shown;  // what the canvas has
  BOOLEAN Full;   // draw every cell on the next flush
} GRID;

// Sets up a grid over a new W x H canvas, cleared to black.
BOOLEAN
GridInit (
  OUT GRID *G,
  IN INT32  W,
  IN INT32  H
);

VOID
GridFree (IN OUT GRID *G);

// Blanks every cell. Nothing is drawn until GridFlush.
VOID
GridClear (IN OUT GRID *G);

VOID
GridPutc (
  IN OUT GRID *G,
  IN UINT32    X,
  IN UINT32    Y,
  IN UINT8     Glyph,
  IN ATTR      Attr
);

// Writes UTF-8 Text from column X, stopping at the right edge. Characters
// CP437 lacks are drawn as '?'. Returns the columns written.
UINT32
GridPuts (
  IN OUT GRID    *G,
  IN UINT32       X,
  IN UINT32       Y,
  IN CONST CHAR8 *Text,
  IN ATTR         Attr
);

// Writes Text so its last character is on column XEnd - 1.
VOID
GridPutsRight (
  IN OUT GRID    *G,
  IN UINT32       XEnd,
  IN UINT32       Y,
  IN CONST CHAR8 *Text,
  IN ATTR         Attr
);

// Writes Text from column X on row Y, wrapped at spaces to Width columns,
// on at most MaxRows rows. Returns the rows used.
UINT32
GridWrap (
  IN OUT GRID    *G,
  IN UINT32       X,
  IN UINT32       Y,
  IN UINT32       Width,
  IN UINT32       MaxRows,
  IN CONST CHAR8 *Text,
  IN ATTR         Attr
);

// A horizontal rule across row Y, from column 1 to the second last.
VOID
GridRule (
  IN OUT GRID *G,
  IN UINT32    Y,
  IN UINT8     Glyph,
  IN ATTR      Attr
);

// The columns Text takes.
UINT32
GridWidth (IN CONST CHAR8 *Text);

// Draws the changed cells, or with Everything the whole canvas, and
// returns FALSE if nothing was drawn. Area is what changed.
BOOLEAN
GridFlush (
  IN OUT GRID    *G,
  IN BOOLEAN      Everything,
  OUT GRID_AREA  *Area
);

#endif
