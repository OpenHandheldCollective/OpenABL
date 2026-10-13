/* SPDX-License-Identifier: GPL-3.0-or-later AND Apache-2.0 */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Rasteriser after font-rs, Copyright 2015 Google Inc., and
 * ab_glyph_rasterizer, Copyright (C) 2020 Alex Butler; ported to C.
 */

/**
  Raster - anti-aliased outline rasteriser.
*/

#include "Gfx/Gfx.h"

#include "Base/Memory.h"

VOID
RasterLine (
  IN OUT RASTER *Raster,
  IN float       X0,
  IN float       Y0,
  IN float       X1,
  IN float       Y1
)
{
  float Dir;
  float Ax;
  float Ay;
  float Bx;
  float By;

  if (FabsF (Y0 - Y1) <= 1e-7f) {
    return;
  }
  // Clamp at the first column. Coverage left of it would land in the last
  // cell of the row above and shade the rest of this row. Rounding can put
  // a shape that starts at 0 there.
  if (X0 < 0) {
    X0 = 0;
  }
  if (X1 < 0) {
    X1 = 0;
  }
  // A is the upper end and B the lower. Dir is the sign of the coverage.
  if (Y0 < Y1) {
    Dir = 1.0f;
    Ax = X0, Ay = Y0, Bx = X1, By = Y1;
  } else {
    Dir = -1.0f;
    Ax = X1, Ay = Y1, Bx = X0, By = Y0;
  }
  float Dxdy     = (Bx - Ax) / (By - Ay);
  float X        = Ax;
  INT32 StartRow = Ay < 0 ? 0 : (INT32)Ay;
  if (Ay < 0) {
    X -= Ay * Dxdy;
  }
  INT32 EndRow = (INT32)CeilF (By);
  if (EndRow > Raster->H) {
    EndRow = Raster->H;
  }

  for (INT32 Y = StartRow; Y < EndRow; Y++) {
    INTN  RowStart  = (INTN)Y * Raster->W;
    float Top       = (float)Y > Ay ? (float)Y : Ay;
    float Bottom    = (float)(Y + 1) < By ? (float)(Y + 1) : By;
    float Dy        = Bottom - Top;
    float NextX     = X + Dxdy * Dy;
    float Coverage  = Dy * Dir;
    float MinX      = X < NextX ? X : NextX;
    float MaxX      = X < NextX ? NextX : X;
    float MinXFloor = FloorF (MinX);
    INT32 MinCell   = (INT32)MinXFloor;
    float MaxXCeil  = CeilF (MaxX);
    INT32 MaxCell   = (INT32)MaxXCeil;
    INTN  At        = RowStart + MinCell;

    if (At < 0 || MaxCell > Raster->W + 1) {
      X = NextX;
      continue;
    }
    if (MaxCell <= MinCell + 1) {
      float MidFrac = 0.5f * (X + NextX) - MinXFloor;
      Raster->Cells[At] += Coverage - Coverage * MidFrac;
      Raster->Cells[At + 1] += Coverage * MidFrac;
    } else {
      float InvSpan  = 1.0f / (MaxX - MinX);
      float MinXFrac = MinX - MinXFloor;
      float A0       = 0.5f * InvSpan * (1.0f - MinXFrac) * (1.0f - MinXFrac);
      float MaxXFrac = MaxX - MaxXCeil + 1.0f;
      float Am       = 0.5f * InvSpan * MaxXFrac * MaxXFrac;

      Raster->Cells[At] += Coverage * A0;
      if (MaxCell == MinCell + 2) {
        Raster->Cells[At + 1] += Coverage * (1.0f - A0 - Am);
      } else {
        float A1 = InvSpan * (1.5f - MinXFrac);
        Raster->Cells[At + 1] += Coverage * (A1 - A0);
        for (INT32 Cell = MinCell + 2; Cell < MaxCell - 1; Cell++) {
          Raster->Cells[RowStart + Cell] += Coverage * InvSpan;
        }
        float A2 = A1 + (float)(MaxCell - MinCell - 3) * InvSpan;
        Raster->Cells[RowStart + MaxCell - 1] += Coverage * (1.0f - A2 - Am);
      }
      Raster->Cells[RowStart + MaxCell] += Coverage * Am;
    }
    X = NextX;
  }
}

VOID
RasterQuad (
  IN OUT RASTER *Raster,
  IN float       X0,
  IN float       Y0,
  IN float       X1,
  IN float       Y1,
  IN float       X2,
  IN float       Y2
)
{
  float Devx  = X0 - 2.0f * X1 + X2;
  float Devy  = Y0 - 2.0f * Y1 + Y2;
  float Devsq = Devx * Devx + Devy * Devy;

  if (Devsq < 0.333f) {
    RasterLine (Raster, X0, Y0, X2, Y2);
    return;
  }
  INT32 Count = 1 + (INT32)FloorF (SqrtF (SqrtF (3.0f * Devsq)));
  float LastX = X0;
  float LastY = Y0;
  float Step  = 1.0f / (float)Count;
  float T     = 0;
  for (INT32 Index = 0; Index < Count - 1; Index++) {
    T += Step;
    float Ax    = X0 + (X1 - X0) * T;
    float Ay    = Y0 + (Y1 - Y0) * T;
    float Bx    = X1 + (X2 - X1) * T;
    float By    = Y1 + (Y2 - Y1) * T;
    float NextX = Ax + (Bx - Ax) * T;
    float NextY = Ay + (By - Ay) * T;
    RasterLine (Raster, LastX, LastY, NextX, NextY);
    LastX = NextX, LastY = NextY;
  }
  RasterLine (Raster, LastX, LastY, X2, Y2);
}

// Splits the curve in half until each piece is flat to a tenth of a pixel,
// or 12 levels deep, and adds the pieces as lines.
STATIC VOID
Cubic (
  IN OUT RASTER *Raster,
  IN float       X0,
  IN float       Y0,
  IN float       X1,
  IN float       Y1,
  IN float       X2,
  IN float       Y2,
  IN float       X3,
  IN float       Y3,
  IN INT32       Depth
)
{
  float Dx1 = X1 - (2 * X0 + X3) / 3;
  float Dy1 = Y1 - (2 * Y0 + Y3) / 3;
  float Dx2 = X2 - (X0 + 2 * X3) / 3;
  float Dy2 = Y2 - (Y0 + 2 * Y3) / 3;

  if (Depth >= 12 ||
      (Dx1 * Dx1 + Dy1 * Dy1 < 0.01f && Dx2 * Dx2 + Dy2 * Dy2 < 0.01f)) {
    RasterLine (Raster, X0, Y0, X3, Y3);
    return;
  }
  float Ax  = (X0 + X1) / 2;
  float Ay  = (Y0 + Y1) / 2;
  float Bx  = (X1 + X2) / 2;
  float By  = (Y1 + Y2) / 2;
  float Cx  = (X2 + X3) / 2;
  float Cy  = (Y2 + Y3) / 2;
  float Abx = (Ax + Bx) / 2;
  float Aby = (Ay + By) / 2;
  float Bcx = (Bx + Cx) / 2;
  float Bcy = (By + Cy) / 2;
  float Mx  = (Abx + Bcx) / 2;
  float My  = (Aby + Bcy) / 2;
  Cubic (Raster, X0, Y0, Ax, Ay, Abx, Aby, Mx, My, Depth + 1);
  Cubic (Raster, Mx, My, Bcx, Bcy, Cx, Cy, X3, Y3, Depth + 1);
}

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
)
{
  Cubic (Raster, X0, Y0, X1, Y1, X2, Y2, X3, Y3, 0);
}

VOID
RasterMask (
  IN CONST RASTER *Raster,
  OUT UINT8       *Mask,
  IN UINTN         Stride
)
{
  float Sum = 0;

  for (INT32 Y = 0; Y < Raster->H; Y++) {
    for (INT32 X = 0; X < Raster->W; X++) {
      Sum += Raster->Cells[(INTN)Y * Raster->W + X];
      float Coverage = FabsF (Sum);
      Mask[(UINTN)Y * Stride + (UINTN)X] =
        Coverage >= 1.0f ? 255 : (UINT8)(Coverage * 255.0f + 0.5f);
    }
  }
}

VOID
SinCos (
  IN float   Angle,
  OUT float *Sin,
  OUT float *Cos
)
{
  CONST float Frac2Pi = 0.63661977f, FracPi2 = 1.57079633f;
  float       Quadrant = RoundF (Angle * Frac2Pi);
  // Angle reduced to within pi/4 of 0.
  float X  = Angle - Quadrant * FracPi2;
  float X2 = X * X;
  float SinR =
    X * (1.0f - X2 / 6.0f * (1.0f - X2 / 20.0f * (1.0f - X2 / 42.0f)));
  float CosR =
    1.0f -
    X2 / 2.0f * (1.0f - X2 / 12.0f * (1.0f - X2 / 30.0f * (1.0f - X2 / 56.0f)));

  switch ((INT32)Quadrant & 3) {
  case 0:
    *Sin = SinR, *Cos = CosR;
    break;
  case 1:
    *Sin = CosR, *Cos = -SinR;
    break;
  case 2:
    *Sin = -SinR, *Cos = -CosR;
    break;
  default:
    *Sin = -CosR, *Cos = SinR;
    break;
  }
}

BOOLEAN
RasterStart (
  IN INT32    W,
  IN INT32    H,
  OUT RASTER *Raster,
  OUT MASK  **Mask
)
{
  *Mask = AllocatePool (sizeof (MASK) + (UINTN)W * H);
  *Raster =
    (RASTER){.W     = W,
             .H     = H,
             .Cells = AllocateZeroArray (RasterCells (W, H), sizeof (float))};
  if (!*Mask || !Raster->Cells) {
    FreePool (*Mask);
    FreePool (Raster->Cells);
    return FALSE;
  }
  return TRUE;
}

MASK *
RasterFinish (
  IN OUT RASTER *Raster,
  IN OUT MASK   *Mask
)
{
  *Mask = (MASK){.W = Raster->W, .H = Raster->H, .Data = (UINT8 *)(Mask + 1)};
  RasterMask (Raster, Mask->Data, (UINTN)Raster->W);
  FreePool (Raster->Cells);
  return Mask;
}
