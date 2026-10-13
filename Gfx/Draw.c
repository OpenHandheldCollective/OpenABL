/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Draw - drawing onto a 32-bit frame and presenting it on the panel.
*/

#include "Gfx/Gfx.h"

VOID
CanvasInit (
  OUT CANVAS *C,
  IN UINT32  *Px,
  IN INT32    W,
  IN INT32    H
)
{
  *C = (CANVAS){.Pixels = Px, .W = W, .H = H};
  CanvasUnclip (C);
}

VOID
CanvasUnclip (IN OUT CANVAS *C)
{
  C->X0 = C->Y0 = 0;
  C->X1         = C->W;
  C->Y1         = C->H;
}

// Blends Dest towards Source by Alpha (0..255) in each channel.
STATIC UINT32
Mix (
  IN UINT32 Dest,
  IN UINT32 Source,
  IN UINT32 Alpha
)
{
  UINT32 Out = 0;
  for (INT32 Shift = 0; Shift < 24; Shift += 8) {
    INT32 DestChannel   = (INT32)(Dest >> Shift & 0xff);
    INT32 SourceChannel = (INT32)(Source >> Shift & 0xff);
    Out |= (UINT32)(DestChannel +
                    ((SourceChannel - DestChannel) * (INT32)Alpha + 127) / 255)
           << Shift;
  }
  return Out;
}

VOID
FillRect (
  IN OUT CANVAS *C,
  IN INT32       X,
  IN INT32       Y,
  IN INT32       W,
  IN INT32       H,
  IN UINT32      Color,
  IN UINT8       Opacity
)
{
  INT32 Left   = X < C->X0 ? C->X0 : X;
  INT32 Top    = Y < C->Y0 ? C->Y0 : Y;
  INT32 Right  = X + W > C->X1 ? C->X1 : X + W;
  INT32 Bottom = Y + H > C->Y1 ? C->Y1 : Y + H;

  for (INT32 Line = Top; Line < Bottom; Line++) {
    UINT32 *Row = C->Pixels + (UINTN)Line * C->W;
    for (INT32 Column = Left; Column < Right; Column++) {
      Row[Column] = Opacity == 255 ? Color : Mix (Row[Column], Color, Opacity);
    }
  }
}

VOID
Blend (
  IN OUT CANVAS *C,
  IN INT32       X,
  IN INT32       Y,
  IN CONST MASK *Mask OPTIONAL,
  IN UINT32      Color,
  IN UINT8       Opacity
)
{
  if (!Mask) {
    return;
  }
  INT32 Left   = X < C->X0 ? C->X0 : X;
  INT32 Top    = Y < C->Y0 ? C->Y0 : Y;
  INT32 Right  = X + Mask->W > C->X1 ? C->X1 : X + Mask->W;
  INT32 Bottom = Y + Mask->H > C->Y1 ? C->Y1 : Y + Mask->H;

  for (INT32 Line = Top; Line < Bottom; Line++) {
    UINT32      *Row     = C->Pixels + (UINTN)Line * C->W;
    CONST UINT8 *MaskRow = Mask->Data + (UINTN)(Line - Y) * Mask->W - X;
    for (INT32 Column = Left; Column < Right; Column++) {
      UINT32 Alpha = MaskRow[Column];
      if (!Alpha) {
        continue;
      }
      Alpha       = Opacity == 255 ? Alpha : Alpha * Opacity / 255;
      Row[Column] = Alpha == 255 ? Color : Mix (Row[Column], Color, Alpha);
    }
  }
}

VOID
DrawText (
  IN OUT CANVAS  *C,
  IN float        X,
  IN float        Baseline,
  IN float        Size,
  IN FONT_WEIGHT  Weight,
  IN CONST CHAR8 *String,
  IN UINT32       Color
)
{
  INT32 Line = (INT32)RoundF (Baseline);

  while (*String) {
    UINTN             Length;
    CONST FONT_GLYPH *Glyph =
      FontGlyph (Size, Weight, Utf8Next (String, &Length), TRUE);

    if (Glyph && Glyph->Mask) {
      MASK Mask = {Glyph->W, Glyph->H, Glyph->Mask};

      Blend (
        C, (INT32)RoundF (X) + Glyph->Left, Line + Glyph->Top, &Mask, Color, 255
      );
    }
    X += Glyph ? Glyph->Advance : 0;
    String += Length;
  }
}

float
TextAdvance (
  IN float        Size,
  IN FONT_WEIGHT  Weight,
  IN CONST CHAR8 *String
)
{
  float Advance = 0;

  while (*String) {
    UINTN             Length;
    CONST FONT_GLYPH *Glyph =
      FontGlyph (Size, Weight, Utf8Next (String, &Length), FALSE);

    Advance += Glyph ? Glyph->Advance : 0;
    String += Length;
  }
  return Advance;
}

VOID
LogicalSize (
  IN UINT32   PanelW,
  IN UINT32   PanelH,
  IN UINT16   Rotation,
  OUT UINT32 *W,
  OUT UINT32 *H
)
{
  BOOLEAN Turned = Rotation % 180 == 90;
  *W             = Turned ? PanelH : PanelW;
  *H             = Turned ? PanelW : PanelH;
}

VOID
Present (
  IN CONST CANVAS *C,
  IN UINT16        Rotation,
  OUT UINT32      *Scratch,
  IN PRESENT_BLIT  Out
)
{
  UINT32 W = (UINT32)C->W;
  UINT32 H = (UINT32)C->H;

  switch (Rotation % 360) {
  case 0:
    Out (C->Pixels, W, 0, 0, W, H);
    return;
  case 90:
    // (X, Y) -> (H - 1 - Y, X). The panel is H wide.
    for (UINT32 Y = 0; Y < H; Y++) {
      for (UINT32 X = 0; X < W; X++) {
        Scratch[(UINTN)X * H + (H - 1 - Y)] = C->Pixels[(UINTN)Y * W + X];
      }
    }
    Out (Scratch, H, 0, 0, H, W);
    return;
  case 180:
    for (UINT32 Y = 0; Y < H; Y++) {
      for (UINT32 X = 0; X < W; X++) {
        Scratch[(UINTN)(H - 1 - Y) * W + (W - 1 - X)] =
          C->Pixels[(UINTN)Y * W + X];
      }
    }
    Out (Scratch, W, 0, 0, W, H);
    return;
  default:
    // 270: (X, Y) -> (Y, W - 1 - X).
    for (UINT32 Y = 0; Y < H; Y++) {
      for (UINT32 X = 0; X < W; X++) {
        Scratch[(UINTN)(W - 1 - X) * H + Y] = C->Pixels[(UINTN)Y * W + X];
      }
    }
    Out (Scratch, H, 0, 0, H, W);
    return;
  }
}
