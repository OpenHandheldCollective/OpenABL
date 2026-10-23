/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present PortareOS
 *   https://github.com/portare-ch
 */

/**
  Grid - the menu's text grid (View/Grid.h).
*/

#include "View/Grid.h"
#include "Base/Memory.h"
#include "View/Vga.h"

// portarelauncher's amber palette: black, then DIM, MID, TEXT and BRIGHT.
STATIC CONST UINT32 mPalette[ATTR_COUNT] = {
  0x000000, 0x663d00, 0xb37400, 0xffb000, 0xffd98a
};

BOOLEAN
GridInit (
  OUT GRID *G,
  IN INT32  W,
  IN INT32  H
)
{
  UINT32 ScaleW = (UINT32)W / (VGA_W * GRID_MIN_COLS);
  UINT32 ScaleH = (UINT32)H / (VGA_H * GRID_MIN_ROWS);

  ZeroMem (G, sizeof (*G));
  G->W     = W;
  G->H     = H;
  G->Scale = ScaleW < ScaleH ? ScaleW : ScaleH;
  if (G->Scale == 0) {
    G->Scale = 1;
  }
  G->Cols = (UINT32)W / (VGA_W * G->Scale);
  G->Rows = (UINT32)H / (VGA_H * G->Scale);
  if (G->Cols == 0 || G->Rows == 0) {
    return FALSE;
  }
  G->Ox     = (W - (INT32)(G->Cols * VGA_W * G->Scale)) / 2;
  G->Oy     = (H - (INT32)(G->Rows * VGA_H * G->Scale)) / 2;
  G->Pixels = AllocateZeroPool ((UINTN)W * (UINTN)H * sizeof (UINT32));
  G->Cur    = AllocateZeroPool ((UINTN)G->Cols * G->Rows * sizeof (CELL));
  G->Shown  = AllocateZeroPool ((UINTN)G->Cols * G->Rows * sizeof (CELL));
  if (!G->Pixels || !G->Cur || !G->Shown) {
    GridFree (G);
    return FALSE;
  }
  GridClear (G);
  G->Full = TRUE;
  return TRUE;
}

VOID
GridFree (IN OUT GRID *G)
{
  FreePool (G->Pixels);
  FreePool (G->Cur);
  FreePool (G->Shown);
  G->Pixels = NULL;
  G->Cur    = NULL;
  G->Shown  = NULL;
}

VOID
GridClear (IN OUT GRID *G)
{
  for (UINTN Index = 0; Index < (UINTN)G->Cols * G->Rows; Index++) {
    G->Cur[Index] = (CELL){' ', ATTR_TEXT};
  }
}

VOID
GridPutc (
  IN OUT GRID *G,
  IN UINT32    X,
  IN UINT32    Y,
  IN UINT8     Glyph,
  IN ATTR      Attr
)
{
  if (X < G->Cols && Y < G->Rows) {
    G->Cur[(UINTN)Y * G->Cols + X] = (CELL){Glyph, (UINT8)Attr};
  }
}

// The next character of UTF-8 Text as a CP437 glyph, advancing Text. The
// menu's strings are ASCII but for a few marks from the old menu's.
STATIC UINT8
NextGlyph (IN OUT CONST CHAR8 **Text)
{
  CONST UINT8 *P = (CONST UINT8 *)*Text;
  UINT32       C;
  UINTN        Length;

  if (P[0] < 0x80) {
    *Text += 1;
    return P[0];
  }
  if ((P[0] & 0xE0) == 0xC0 && P[1]) {
    C      = (UINT32)(P[0] & 0x1F) << 6 | (P[1] & 0x3F);
    Length = 2;
  } else if ((P[0] & 0xF0) == 0xE0 && P[1] && P[2]) {
    C      = (UINT32)(P[0] & 0x0F) << 12 | (UINT32)(P[1] & 0x3F) << 6 | (P[2] & 0x3F);
    Length = 3;
  } else {
    *Text += 1;
    return '?';
  }
  *Text += Length;
  switch (C) {
  case 0x00B0:
    return 0xF8; // degree sign
  case 0x00B7:
    return 0xFA; // middle dot
  case 0x00D7:
    return 'x';
  case 0x2019:
    return '\'';
  default:
    return '?';
  }
}

UINT32
GridWidth (IN CONST CHAR8 *Text)
{
  UINT32 Width = 0;

  while (*Text) {
    NextGlyph (&Text);
    Width++;
  }
  return Width;
}

UINT32
GridPuts (
  IN OUT GRID    *G,
  IN UINT32       X,
  IN UINT32       Y,
  IN CONST CHAR8 *Text,
  IN ATTR         Attr
)
{
  UINT32 Start = X;

  while (*Text && X < G->Cols) {
    GridPutc (G, X++, Y, NextGlyph (&Text), Attr);
  }
  return X - Start;
}

VOID
GridPutsRight (
  IN OUT GRID    *G,
  IN UINT32       XEnd,
  IN UINT32       Y,
  IN CONST CHAR8 *Text,
  IN ATTR         Attr
)
{
  UINT32 Width = GridWidth (Text);

  if (Width <= XEnd) {
    GridPuts (G, XEnd - Width, Y, Text, Attr);
  }
}

UINT32
GridWrap (
  IN OUT GRID    *G,
  IN UINT32       X,
  IN UINT32       Y,
  IN UINT32       Width,
  IN UINT32       MaxRows,
  IN CONST CHAR8 *Text,
  IN ATTR         Attr
)
{
  UINT32 Rows = 0;

  while (*Text && Rows < MaxRows) {
    // The longest run of whole words that fits, or a hard break inside a
    // word longer than the line.
    CONST CHAR8 *Scan  = Text;
    CONST CHAR8 *Break = NULL;
    UINT32       Used  = 0;

    while (*Scan && Used < Width) {
      if (*Scan == ' ') {
        Break = Scan;
      }
      NextGlyph (&Scan);
      Used++;
    }
    if (!*Scan || *Scan == ' ') {
      Break = Scan;
    } else if (!Break) {
      Break = Scan;
    }
    for (UINT32 Column = X; Text < Break; Column++) {
      GridPutc (G, Column, Y + Rows, NextGlyph (&Text), Attr);
    }
    while (*Text == ' ') {
      Text++;
    }
    Rows++;
  }
  return Rows;
}

VOID
GridRule (
  IN OUT GRID *G,
  IN UINT32    Y,
  IN UINT8     Glyph,
  IN ATTR      Attr
)
{
  for (UINT32 X = 1; X + 1 < G->Cols; X++) {
    GridPutc (G, X, Y, Glyph, Attr);
  }
}

STATIC VOID
DrawCell (
  IN OUT GRID *G,
  IN UINT32    Cx,
  IN UINT32    Cy
)
{
  CONST CELL  *Cell  = &G->Cur[(UINTN)Cy * G->Cols + Cx];
  CONST UINT8 *Rows  = gVga[Cell->Glyph];
  UINT32       Fg    = mPalette[Cell->Attr < ATTR_COUNT ? Cell->Attr : ATTR_TEXT];
  UINT32       Scale = G->Scale;
  INT32        Px    = G->Ox + (INT32)(Cx * VGA_W * Scale);
  INT32        Py    = G->Oy + (INT32)(Cy * VGA_H * Scale);

  for (UINT32 Gy = 0; Gy < VGA_H; Gy++) {
    for (UINT32 Sy = 0; Sy < Scale; Sy++) {
      UINT32 *Out = G->Pixels + (UINTN)(Py + (INT32)(Gy * Scale + Sy)) * (UINTN)G->W + (UINTN)Px;
      for (UINT32 Gx = 0; Gx < VGA_W; Gx++) {
        UINT32 Color = Rows[Gy] & (0x80 >> Gx) ? Fg : mPalette[ATTR_BG];
        for (UINT32 Sx = 0; Sx < Scale; Sx++) {
          *Out++ = Color;
        }
      }
    }
  }
}

BOOLEAN
GridFlush (
  IN OUT GRID    *G,
  IN BOOLEAN      Everything,
  OUT GRID_AREA  *Area
)
{
  UINT32 CellW   = VGA_W * G->Scale;
  UINT32 CellH   = VGA_H * G->Scale;
  UINT32 MinX    = G->Cols, MinY = G->Rows, MaxX = 0, MaxY = 0;
  BOOLEAN All    = Everything || G->Full;
  BOOLEAN Drawn  = FALSE;

  for (UINT32 Cy = 0; Cy < G->Rows; Cy++) {
    for (UINT32 Cx = 0; Cx < G->Cols; Cx++) {
      UINTN Index = (UINTN)Cy * G->Cols + Cx;
      if (!All && G->Cur[Index].Glyph == G->Shown[Index].Glyph &&
          G->Cur[Index].Attr == G->Shown[Index].Attr) {
        continue;
      }
      DrawCell (G, Cx, Cy);
      G->Shown[Index] = G->Cur[Index];
      MinX  = Cx < MinX ? Cx : MinX;
      MinY  = Cy < MinY ? Cy : MinY;
      MaxX  = Cx > MaxX ? Cx : MaxX;
      MaxY  = Cy > MaxY ? Cy : MaxY;
      Drawn = TRUE;
    }
  }
  G->Full = FALSE;
  if (All) {
    // The border around the grid too, so a full flush covers the canvas.
    *Area = (GRID_AREA){0, 0, G->W, G->H};
    return TRUE;
  }
  if (!Drawn) {
    return FALSE;
  }
  *Area = (GRID_AREA){
    G->Ox + (INT32)(MinX * CellW), G->Oy + (INT32)(MinY * CellH),
    G->Ox + (INT32)((MaxX + 1) * CellW), G->Oy + (INT32)((MaxY + 1) * CellH)
  };
  return TRUE;
}
