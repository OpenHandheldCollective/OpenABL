/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Shape - rounded rectangles and rings as coverage masks.
*/

#include "Base/OablBase.h"

#include "Gfx/Gfx.h"
#include "Base/Buffer.h"
#include "Base/Memory.h"

typedef struct _CACHED {
  INT32 W, H, Radius, Width;
  MASK *Mask;
} CACHED;

// Masks made so far, kept until ShapesReset.
STATIC struct OABL_VEC (CACHED) mCache;

// Adds the outline of a rounded rectangle inset by Inset. Hole winds it the
// other way, so it cuts a hole in an outline wound forwards.
STATIC VOID
Outline (
  IN OUT RASTER *Raster,
  IN float       W,
  IN float       H,
  IN float       Radius,
  IN float       Inset,
  IN BOOLEAN     Hole
)
{
  float X0           = Inset;
  float Y0           = Inset;
  float X1           = W - Inset;
  float Y1           = H - Inset;
  float CornerRadius = Radius - Inset;
  float Points[4 * 33][2];
  INT32 Count = 0;
  INT32 Steps;

  if (CornerRadius < 0) {
    CornerRadius = 0;
  }
  Steps = CornerRadius < 2    ? 1
          : CornerRadius > 64 ? 32
                              : (INT32)(CornerRadius / 2) + 1;
  CONST float Corners[4][3] = {{X1 - CornerRadius, Y0 + CornerRadius, -90},
                               {X1 - CornerRadius, Y1 - CornerRadius, 0},
                               {X0 + CornerRadius, Y1 - CornerRadius, 90},
                               {X0 + CornerRadius, Y0 + CornerRadius, 180}};
  for (INT32 Corner = 0; Corner < 4; Corner++) {
    for (INT32 Index = 0; Index <= Steps; Index++) {
      float Sin;
      float Cos;
      SinCos (
        (Corners[Corner][2] + 90.0f * (float)Index / (float)Steps) *
          3.14159265f / 180.0f,
        &Sin, &Cos
      );
      Points[Count][0] = Corners[Corner][0] + CornerRadius * Cos;
      Points[Count][1] = Corners[Corner][1] + CornerRadius * Sin;
      Count++;
    }
  }
  for (INT32 Index = 0; Index < Count; Index++) {
    CONST float *A = Points[Index], *B = Points[(Index + 1) % Count];
    if (Hole) {
      RasterLine (Raster, B[0], B[1], A[0], A[1]);
    } else {
      RasterLine (Raster, A[0], A[1], B[0], B[1]);
    }
  }
}

CONST MASK *
RoundRectMask (
  IN INT32 W,
  IN INT32 H,
  IN INT32 Radius,
  IN INT32 Width
)
{
  if (W <= 0 || H <= 0) {
    return NULL;
  }
  if (Radius > W / 2) {
    Radius = W / 2;
  }
  if (Radius > H / 2) {
    Radius = H / 2;
  }
  for (UINTN Index = 0; Index < mCache.Count; Index++) {
    CACHED *Entry = &mCache.Items[Index];
    if (Entry->W == W && Entry->H == H && Entry->Radius == Radius &&
        Entry->Width == Width) {
      return Entry->Mask;
    }
  }
  MASK  *Mask;
  RASTER Raster;

  if (!RasterStart (W, H, &Raster, &Mask)) {
    return NULL;
  }
  Outline (&Raster, (float)W, (float)H, (float)Radius, 0, FALSE);
  if (Width > 0) {
    Outline (&Raster, (float)W, (float)H, (float)Radius, (float)Width, TRUE);
  }
  RasterFinish (&Raster, Mask);
  // A mask that can't be cached would never be freed, so drop it.
  if (!VecPush (&mCache, (CACHED){W, H, Radius, Width, Mask})) {
    FreePool (Mask);
    return NULL;
  }
  return Mask;
}

VOID
ShapesReset (VOID)
{
  for (UINTN Index = 0; Index < mCache.Count; Index++) {
    FreePool (mCache.Items[Index].Mask);
  }
  mCache.Count = 0;
}
