/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gfx - font, icons, logo and rounded shapes, drawn without a library.

  Everything is rasterised into coverage masks, which the view and the boot
  splash draw in any colour onto a 32-bit frame and present, rotated, on
  the panel.
*/

#ifndef __GFX_GFX_H__
#define __GFX_GFX_H__

#include "Base/OablBase.h"

#include "Ui/Ui.h"

// Coverage mask, drawn later in any colour.
typedef struct _MASK {
  INT32  W, H;
  UINT8 *Data; // W x H, 0..255
} MASK;

// Anti-aliased rasteriser using the signed-area accumulation of font-rs and
// ab_glyph_rasterizer. Lines add coverage to the cells they cross, and a
// running sum along each row turns it into the filled area.

typedef struct _RASTER {
  INT32  W, H;
  float *Cells; // RasterCells (W, H) cells
} RASTER;

// One cell per pixel, plus a few for the coverage lines leave right of the
// last column.
STATIC inline UINTN
RasterCells (
  IN INT32 W,
  IN INT32 H
)
{
  return (UINTN)W * (UINTN)H + 4;
}

// Adds a line, a quadratic or a cubic Bezier. Coordinates are in pixels.
VOID
RasterLine (
  IN OUT RASTER *Raster,
  IN float       X0,
  IN float       Y0,
  IN float       X1,
  IN float       Y1
);

VOID
RasterQuad (
  IN OUT RASTER *Raster,
  IN float       X0,
  IN float       Y0,
  IN float       X1,
  IN float       Y1,
  IN float       X2,
  IN float       Y2
);

VOID
RasterCubic (
  IN OUT RASTER *Raster,
  IN float       X0,
  IN float       Y0,
  IN float       X1,
  IN float       Y1,
  IN float       X2,
  IN float       Y2,
  IN float       X3,
  IN float       Y3
);

VOID
RasterMask (
  IN CONST RASTER *Raster,
  OUT UINT8       *Mask,
  IN UINTN         Stride
);

// Allocates a zeroed W x H raster and its mask, or nothing on failure.
BOOLEAN
RasterStart (
  IN INT32    W,
  IN INT32    H,
  OUT RASTER *Raster,
  OUT MASK  **Mask
);

// Writes the coverage into Mask, frees the raster and returns Mask.
MASK *
RasterFinish (
  IN OUT RASTER *Raster,
  IN OUT MASK   *Mask
);

// Computes the sine and cosine of Angle (radians) with Taylor polynomials,
// after reducing it to within pi/4 of a multiple of pi/2.
VOID
SinCos (
  IN float   Angle,
  OUT float *Sin,
  OUT float *Cos
);

STATIC inline float
FloorF (IN float Value)
{
  return __builtin_floorf (Value);
}

STATIC inline float
CeilF (IN float Value)
{
  return __builtin_ceilf (Value);
}

STATIC inline float
RoundF (IN float Value)
{
  return __builtin_roundf (Value);
}

STATIC inline float
SqrtF (IN float Value)
{
  return __builtin_sqrtf (Value);
}

STATIC inline float
FabsF (IN float Value)
{
  return __builtin_fabsf (Value);
}

// Rounded rectangles and rings, cached per size until ShapesReset.

// Returns a filled W x H rounded rectangle, or with Width > 0 an outline
// that wide inside it. A radius of half the side gives a disc.
CONST MASK *
RoundRectMask (
  IN INT32 W,
  IN INT32 H,
  IN INT32 Radius,
  IN INT32 Width
);

// Frees the masks. Nothing may still be drawing from them.
VOID
ShapesReset (VOID);

// Line icons on a 24-unit grid with a 2-unit round stroke, as in Tabler
// Icons. Each size is rasterised on first use.

// Returns Icon at Size pixels in a square mask padded by IconPad (Size).
// Draw it at (Cx - Size / 2 - Pad, Cy - Size / 2 - Pad) to centre it on
// (Cx, Cy).
CONST MASK *
IconMask (
  IN ICON  Icon,
  IN float Size
);

// Strokes may reach a stroke's width outside the 24-unit box. The pad
// allows for that plus anti-aliasing.
float
IconPad (IN float Size);

// Frees the masks. Nothing may still be drawing from them.
VOID
IconsReset (VOID);

// The menu font at any size, read in place from the .oablf files in Assets:
// Montserrat at two weights. Glyphs are measured and rasterised on first use and kept.

typedef enum { FONT_WEIGHT_REGULAR, FONT_WEIGHT_MEDIUM } FONT_WEIGHT;

typedef struct _FONT_GLYPH {
  BOOLEAN Measured;
  UINT16  W, H;
  // Offset of the box from the pen, y down from the baseline.
  INT16  Left, Top;
  float  Advance; // in pixels
  UINT8 *Mask;    // W x H coverage, once drawn
} FONT_GLYPH;

// Data must stay in place. Fails if it isn't a .oablf file.
BOOLEAN
FontLoad (
  IN CONST UINT8 *Data,
  IN UINTN        Size
);

// Adds a fallback face for missing characters. Data must stay in place.
BOOLEAN
FontAddFallback (
  IN CONST UINT8 *Data,
  IN UINTN        Size
);

// Returns the glyph for CodePoint at Size pixels per em, or '?' if no face
// has it. The mask is drawn only WithMask.
CONST FONT_GLYPH *
FontGlyph (
  IN float       Size,
  IN FONT_WEIGHT Weight,
  IN UINT32      CodePoint,
  IN BOOLEAN     WithMask
);

// Metrics at Size pixels per em. The descender is negative.
float
FontCapHeight (IN float Size);

float
FontAscender (IN float Size);

float
FontDescender (IN float Size);

// Decodes the next code point of String. Sequences longer than three bytes
// come back a byte at a time, as '?'.
UINT32
Utf8Next (
  IN CONST CHAR8 *String,
  OUT UINTN      *Length
);

// Drawing onto a 32-bit frame, used by the boot splash.

// Pixels are 0x00RRGGBB, which is GOP's BGRX in memory.
typedef struct _CANVAS {
  UINT32 *Pixels;
  INT32   W, H;
  // Clip rectangle: X0 <= x < X1, Y0 <= y < Y1.
  INT32 X0, Y0, X1, Y1;
} CANVAS;

// Sets up C over the W x H pixels at Px, with no clip.
VOID
CanvasInit (
  OUT CANVAS *C,
  IN UINT32  *Px,
  IN INT32    W,
  IN INT32    H
);

VOID
CanvasUnclip (IN OUT CANVAS *C);

// Opacity 255 is solid.
VOID
FillRect (
  IN OUT CANVAS *C,
  IN INT32       X,
  IN INT32       Y,
  IN INT32       W,
  IN INT32       H,
  IN UINT32      Color,
  IN UINT8       Opacity
);

// Draws Mask in Color with its top-left at (X, Y). NULL draws nothing.
VOID
Blend (
  IN OUT CANVAS *C,
  IN INT32       X,
  IN INT32       Y,
  IN CONST MASK *Mask OPTIONAL,
  IN UINT32      Color,
  IN UINT8       Opacity
);

// Draws String in the menu font, Size pixels per em, starting at X with its
// baseline at Baseline.
VOID
DrawText (
  IN OUT CANVAS  *C,
  IN float        X,
  IN float        Baseline,
  IN float        Size,
  IN FONT_WEIGHT  Weight,
  IN CONST CHAR8 *String,
  IN UINT32       Color
);

// Returns how far DrawText's pen moves for String.
float
TextAdvance (
  IN float        Size,
  IN FONT_WEIGHT  Weight,
  IN CONST CHAR8 *String
);

// Presenting a frame, rotated Rotation degrees clockwise. A portrait panel
// shows a landscape picture at 90 or 270.

// Copies W x H pixels (Stride pixels per row) to (X, Y) on the panel.
typedef VOID (*PRESENT_BLIT) (
  IN CONST UINT32 *Pixels,
  IN UINT32        Stride,
  IN UINT32        X,
  IN UINT32        Y,
  IN UINT32        W,
  IN UINT32        H
);

// Returns the size of a PanelW x PanelH panel rotated by Rotation.
VOID
LogicalSize (
  IN UINT32   PanelW,
  IN UINT32   PanelH,
  IN UINT16   Rotation,
  OUT UINT32 *W,
  OUT UINT32 *H
);

// Presents all of C rotated. Scratch must hold W x H pixels.
VOID
Present (
  IN CONST CANVAS *C,
  IN UINT16        Rotation,
  OUT UINT32      *Scratch,
  IN PRESENT_BLIT  Out
);

#endif
