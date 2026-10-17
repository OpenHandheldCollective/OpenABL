/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Icon designs after Tabler Icons, Copyright (c) Paweł Kuna.
 */

/**
  Icons - line icons, rasterised into a coverage mask per size on first use.
*/

#include "Gfx/Gfx.h"
#include "Base/Buffer.h"
#include "Base/Memory.h"

// Stroke width, in grid units.
#define STROKE     2.0f
#define PI         3.14159265f
#define MAX_POINTS 160

typedef float POINT[2];

// Scales grid units by Scale and moves them by (Ox, Oy) into the raster.
typedef struct _PEN {
  RASTER *Raster;
  float   Ox, Oy, Scale;
} PEN;

STATIC VOID
Edge (
  IN CONST PEN   *Pen,
  IN CONST float *A,
  IN CONST float *B
)
{
  RasterLine (
    Pen->Raster, Pen->Ox + A[0] * Pen->Scale, Pen->Oy + A[1] * Pen->Scale,
    Pen->Ox + B[0] * Pen->Scale, Pen->Oy + B[1] * Pen->Scale
  );
}

// Fills a polygon. Every polygon is wound the same way, so overlaps add up
// to full coverage.
STATIC VOID
Polygon (
  IN CONST PEN   *Pen,
  IN CONST POINT *Points,
  IN INT32        Count
)
{
  float Area = 0;

  if (Count < 3) {
    return;
  }
  for (INT32 Index = 0; Index < Count; Index++) {
    CONST float *A = Points[Index], *B = Points[(Index + 1) % Count];
    Area += A[0] * B[1] - B[0] * A[1];
  }
  for (INT32 Index = 0; Index < Count; Index++) {
    CONST float *A = Points[Index], *B = Points[(Index + 1) % Count];
    if (Area > 0) {
      Edge (Pen, B, A);
    } else {
      Edge (Pen, A, B);
    }
  }
}

// Returns the segment count for a circle of Radius, from 12 to 64.
STATIC INT32
CircleSegments (IN float Radius)
{
  INT32 Count = (INT32)(Radius * 2);
  return Count < 12 ? 12 : Count > 64 ? 64 : Count;
}

STATIC VOID
Circle (
  IN CONST PEN *Pen,
  IN float      Cx,
  IN float      Cy,
  IN float      Radius
)
{
  POINT Points[64];
  INT32 Count = CircleSegments (Radius);

  for (INT32 Index = 0; Index < Count; Index++) {
    float Sin;
    float Cos;
    SinCos ((float)Index / (float)Count * 2 * PI, &Sin, &Cos);
    Points[Index][0] = Cx + Radius * Cos;
    Points[Index][1] = Cy + Radius * Sin;
  }
  Polygon (Pen, Points, Count);
}

// Draws a polyline STROKE wide, as a quad per segment and a disc at each
// point.
STATIC VOID
Stroke (
  IN CONST PEN   *Pen,
  IN CONST POINT *Points,
  IN INT32        Count,
  IN BOOLEAN      Closed
)
{
  float HalfWidth = STROKE / 2;
  INT32 Segments  = Closed ? Count : Count - 1;

  for (INT32 Index = 0; Index < Segments; Index++) {
    CONST float *A = Points[Index], *B = Points[(Index + 1) % Count];
    float        Dx     = B[0] - A[0];
    float        Dy     = B[1] - A[1];
    float        Length = SqrtF (Dx * Dx + Dy * Dy);
    if (Length < 1e-4f) {
      continue;
    }
    float Nx      = -Dy / Length * HalfWidth;
    float Ny      = Dx / Length * HalfWidth;
    POINT Quad[4] = {{A[0] + Nx, A[1] + Ny},
                     {B[0] + Nx, B[1] + Ny},
                     {B[0] - Nx, B[1] - Ny},
                     {A[0] - Nx, A[1] - Ny}};
    Polygon (Pen, Quad, 4);
  }
  for (INT32 Index = 0; Index < Count; Index++) {
    Circle (Pen, Points[Index][0], Points[Index][1], HalfWidth);
  }
}

// Writes the points of an elliptical arc from A0 to A1 degrees into Out and
// returns their count.
STATIC INT32
ArcPoints (
  IN float   Cx,
  IN float   Cy,
  IN float   Rx,
  IN float   Ry,
  IN float   A0,
  IN float   A1,
  OUT POINT *Out
)
{
  float Sweep = A1 - A0;
  INT32 Count =
    (INT32)(FabsF (Sweep) / 360.0f * (float)CircleSegments (Rx > Ry ? Rx : Ry));

  if (Count < 4) {
    Count = 4;
  }
  for (INT32 Index = 0; Index <= Count; Index++) {
    float Sin;
    float Cos;
    SinCos (
      (A0 + Sweep * (float)Index / (float)Count) * PI / 180.0f, &Sin, &Cos
    );
    Out[Index][0] = Cx + Rx * Cos;
    Out[Index][1] = Cy + Ry * Sin;
  }
  return Count + 1;
}

STATIC VOID
Arc (
  IN CONST PEN *Pen,
  IN float      Cx,
  IN float      Cy,
  IN float      Rx,
  IN float      Ry,
  IN float      A0,
  IN float      A1
)
{
  POINT Points[MAX_POINTS];
  Stroke (Pen, Points, ArcPoints (Cx, Cy, Rx, Ry, A0, A1, Points), FALSE);
}

STATIC VOID
Ring (
  IN CONST PEN *Pen,
  IN float      Cx,
  IN float      Cy,
  IN float      Rx,
  IN float      Ry
)
{
  POINT Points[MAX_POINTS];
  Stroke (Pen, Points, ArcPoints (Cx, Cy, Rx, Ry, 0, 360, Points), TRUE);
}

STATIC VOID
RoundRect (
  IN CONST PEN *Pen,
  IN float      X,
  IN float      Y,
  IN float      W,
  IN float      H,
  IN float      R
)
{
  POINT Points[MAX_POINTS];
  INT32 Count = ArcPoints (X + W - R, Y + R, R, R, -90, 0, Points);
  Count += ArcPoints (X + W - R, Y + H - R, R, R, 0, 90, Points + Count);
  Count += ArcPoints (X + R, Y + H - R, R, R, 90, 180, Points + Count);
  Count += ArcPoints (X + R, Y + R, R, R, 180, 270, Points + Count);
  Stroke (Pen, Points, Count, TRUE);
}

// LINE and CLOSED stroke an open or a closed polyline through the points.
#define LINE(...)                                                              \
  do {                                                                         \
    STATIC CONST POINT Points[] = __VA_ARGS__;                                 \
    Stroke (                                                                   \
      Pen, Points, (INT32)(sizeof (Points) / sizeof (Points[0])), FALSE        \
    );                                                                         \
  } while (0)
#define CLOSED(...)                                                            \
  do {                                                                         \
    STATIC CONST POINT Points[] = __VA_ARGS__;                                 \
    Stroke (Pen, Points, (INT32)(sizeof (Points) / sizeof (Points[0])), TRUE); \
  } while (0)

STATIC VOID
Shape (
  IN CONST PEN *Pen,
  IN ICON       Icon
)
{
  switch (Icon) {
  case ICON_PLAY:
    CLOSED ({{7, 4}, {20, 12}, {7, 20}});
    break;
  case ICON_REFRESH:
    Arc (Pen, 12, 12, 8, 8, 0, -160);
    LINE ({{4, 5}, {4, 9}, {8, 9}});
    Arc (Pen, 12, 12, 8, 8, 180, 20);
    LINE ({{20, 19}, {20, 15}, {16, 15}});
    break;
  case ICON_LIFEBUOY:
    Ring (Pen, 12, 12, 9, 9);
    Ring (Pen, 12, 12, 4, 4);
    LINE ({{15, 15}, {18.4f, 18.4f}});
    LINE ({{9, 15}, {5.6f, 18.4f}});
    LINE ({{9, 9}, {5.6f, 5.6f}});
    LINE ({{15, 9}, {18.4f, 5.6f}});
    break;
  case ICON_CPU:
    RoundRect (Pen, 5, 5, 14, 14, 1.5f);
    CLOSED ({{9, 9}, {15, 9}, {15, 15}, {9, 15}});
    LINE ({{3, 10}, {5, 10}});
    LINE ({{19, 10}, {21, 10}});
    LINE ({{10, 3}, {10, 5}});
    LINE ({{10, 19}, {10, 21}});
    LINE ({{3, 14}, {5, 14}});
    LINE ({{19, 14}, {21, 14}});
    LINE ({{14, 3}, {14, 5}});
    LINE ({{14, 19}, {14, 21}});
    break;
  case ICON_SWITCH:
    LINE ({{16, 3}, {20, 7}, {16, 11}});
    LINE ({{10, 7}, {20, 7}});
    LINE ({{8, 13}, {4, 17}, {8, 21}});
    LINE ({{4, 17}, {14, 17}});
    break;
  case ICON_TERMINAL:
    LINE ({{5, 7}, {10, 12}, {5, 17}});
    LINE ({{12, 19}, {19, 19}});
    break;
  case ICON_VOLUME:
    CLOSED (
      {{3, 9.5f}, {6.5f, 9.5f}, {11, 5}, {11, 19}, {6.5f, 14.5f}, {3, 14.5f}}
    );
    Arc (Pen, 13, 12, 4, 4, -50, 50);
    Arc (Pen, 13, 12, 7.5f, 7.5f, -55, 55);
    break;
  case ICON_SD_CARD:
    CLOSED ({{6, 8}, {11, 3}, {18, 3}, {18, 21}, {6, 21}});
    LINE ({{10.5f, 6}, {10.5f, 8}});
    LINE ({{13, 6}, {13, 8}});
    LINE ({{15.5f, 6}, {15.5f, 8}});
    break;
  case ICON_BATTERY:
    RoundRect (Pen, 3, 7, 15, 10, 2);
    LINE ({{18, 10}, {20.5f, 10}, {20.5f, 14}, {18, 14}});
    LINE ({{11.5f, 8.5f}, {9.5f, 12}, {12.5f, 12}, {10.5f, 15.5f}});
    break;
  case ICON_GAMEPAD:
    RoundRect (Pen, 2, 6, 20, 12, 4);
    LINE ({{6, 12}, {10, 12}});
    LINE ({{8, 10}, {8, 14}});
    Circle (Pen, 15, 11, 1.1f);
    Circle (Pen, 18, 13, 1.1f);
    break;
  case ICON_TRASH:
    LINE ({{4, 7}, {20, 7}});
    LINE ({{10, 11}, {10, 17}});
    LINE ({{14, 11}, {14, 17}});
    LINE ({{5, 7}, {6, 19}, {7.5f, 20.5f}, {16.5f, 20.5f}, {18, 19}, {19, 7}});
    LINE ({{9, 7}, {9, 4}, {10, 3}, {14, 3}, {15, 4}, {15, 7}});
    break;
  case ICON_POWER:
    Arc (Pen, 12, 12, 7.75f, 7.75f, -50, 230);
    LINE ({{12, 4}, {12, 12}});
    break;
  case ICON_CHEVRON_UP:
    LINE ({{6, 15}, {12, 9}, {18, 15}});
    break;
  case ICON_CHEVRON_DOWN:
    LINE ({{6, 9}, {12, 15}, {18, 9}});
    break;
  case ICON_CHEVRON_RIGHT:
    LINE ({{9, 6}, {15, 12}, {9, 18}});
    break;
  case ICON_SELECTOR:
    LINE ({{8, 9}, {12, 5}, {16, 9}});
    LINE ({{8, 15}, {12, 19}, {16, 15}});
    break;
  case ICON_ARROWS_VERTICAL:
    LINE ({{8, 7}, {12, 3}, {16, 7}});
    LINE ({{8, 17}, {12, 21}, {16, 17}});
    LINE ({{12, 3}, {12, 21}});
    break;
  case ICON_ARROW_BACK:
    LINE ({{9, 14}, {5, 10}, {9, 6}});
    LINE ({{5, 10}, {16, 10}});
    Arc (Pen, 16, 14, 4, 4, -90, 90);
    LINE ({{16, 18}, {15, 18}});
    break;
  case ICON_ARROW_LEFT:
    LINE ({{5, 12}, {19, 12}});
    LINE ({{11, 18}, {5, 12}, {11, 6}});
    break;
  case ICON_INFO:
    Ring (Pen, 12, 12, 9, 9);
    LINE ({{12, 11}, {12, 16}});
    Circle (Pen, 12, 8, 1.25f);
    break;
  case ICON_ALERT:
    CLOSED ({{12, 3.5f}, {21.5f, 20}, {2.5f, 20}});
    LINE ({{12, 9}, {12, 13}});
    Circle (Pen, 12, 16.5f, 1.25f);
    break;
  case ICON_CIRCLE:
    Ring (Pen, 12, 12, 9, 9);
    break;
  case ICON_CIRCLE_CHECK:
    Ring (Pen, 12, 12, 9, 9);
    LINE ({{8.5f, 12}, {11, 14.5f}, {15.5f, 10}});
    break;
  case ICON_DATABASE:
    Ring (Pen, 12, 6, 8, 3);
    LINE ({{4, 6}, {4, 18}});
    LINE ({{20, 6}, {20, 18}});
    Arc (Pen, 12, 12, 8, 3, 0, 180);
    Arc (Pen, 12, 18, 8, 3, 0, 180);
    break;
  case ICON_STACK:
    CLOSED ({{12, 4}, {20, 8}, {12, 12}, {4, 8}});
    LINE ({{4, 12}, {12, 16}, {20, 12}});
    LINE ({{4, 16}, {12, 20}, {20, 16}});
    break;
  case ICON_MONITOR:
    RoundRect (Pen, 3, 4, 18, 12, 1);
    LINE ({{7, 20}, {17, 20}});
    LINE ({{9, 16}, {9, 20}});
    LINE ({{15, 16}, {15, 20}});
    break;
  case ICON_ROTATE:
    Arc (Pen, 12, 12, 8, 8, 187, 518);
    LINE ({{4, 20}, {4, 15}, {9, 15}});
    break;
  case ICON_ENTER:
    LINE ({{18, 6}, {18, 12}, {15, 15}, {5, 15}});
    LINE ({{9, 11}, {5, 15}, {9, 19}});
    break;
  case ICON_CLOCK:
    Ring (Pen, 12, 12, 9, 9);
    LINE ({{12, 7}, {12, 12}, {15, 15}});
    break;
  case ICON_WORLD:
    Ring (Pen, 12, 12, 9, 9);
    Ring (Pen, 12, 12, 3.5f, 9);
    LINE ({{3.6f, 9}, {20.4f, 9}});
    LINE ({{3.6f, 15}, {20.4f, 15}});
    break;
  case ICON_FORK:
    Ring (Pen, 12, 18, 2, 2);
    Ring (Pen, 7, 6, 2, 2);
    Ring (Pen, 17, 6, 2, 2);
    LINE ({{7, 8}, {7, 10}, {9, 12}, {15, 12}, {17, 10}, {17, 8}});
    LINE ({{12, 12}, {12, 16}});
    break;
  case ICON_MESSAGE:
    CLOSED (
      {{4, 6}, {6, 4}, {18, 4}, {20, 6}, {20, 15}, {18, 17}, {13, 17}, {8, 20},
       {8, 17}, {6, 17}, {4, 15}}
    );
    LINE ({{8, 9}, {16, 9}});
    LINE ({{8, 13}, {14, 13}});
    break;
  case ICON_PHOTO:
    RoundRect (Pen, 3, 4, 18, 16, 3);
    LINE ({{3.5f, 16}, {8, 11.5f}, {13, 16.5f}});
    LINE ({{13, 15}, {15, 13}, {20.5f, 18.5f}});
    Circle (Pen, 15, 8.5f, 1.25f);
    break;
  case ICON_USB:
    LINE ({{12, 4}, {12, 21}});
    CLOSED ({{10, 5}, {12, 3}, {14, 5}});
    LINE ({{12, 17}, {7, 14}, {7, 10}});
    LINE ({{12, 15}, {17, 12}, {17, 9}});
    Circle (Pen, 7, 9, 1.5f);
    CLOSED ({{16, 7}, {18, 7}, {18, 9}, {16, 9}});
    Circle (Pen, 12, 20, 1.5f);
    break;
  case ICON_MONITOR_OFF:
    LINE ({{21, 15}, {21, 5}, {20, 4}, {10, 4}});
    LINE ({{6, 4}, {4, 4}, {3, 5}, {3, 15}, {4, 16}, {17, 16}});
    LINE ({{7, 20}, {17, 20}});
    LINE ({{9, 16}, {9, 20}});
    LINE ({{15, 16}, {15, 20}});
    LINE ({{3, 3}, {21, 21}});
    break;
  case ICON_PLUG:
    LINE ({{9, 3}, {9, 7}});
    LINE ({{15, 3}, {15, 7}});
    LINE ({{6, 7}, {18, 7}, {18, 11}, {14, 15}, {10, 15}, {6, 11}, {6, 7}});
    LINE ({{12, 15}, {12, 21}});
    break;
  case ICON_COUNT:
    break;
  }
}

float
IconPad (IN float Size)
{
  return Size / 24.0f * STROKE + 2.0f;
}

typedef struct _CACHED {
  ICON   Icon;
  UINT32 Key;
  MASK  *Mask;
} CACHED;

// Masks made so far, kept until IconsReset.
STATIC struct OABL_VEC (CACHED) mCache;

CONST MASK *
IconMask (
  IN ICON  Icon,
  IN float Size
)
{
  UINT32 Key = (UINT32)(Size * 4.0f + 0.5f);

  for (UINTN Index = 0; Index < mCache.Count; Index++) {
    if (mCache.Items[Index].Icon == Icon && mCache.Items[Index].Key == Key) {
      return mCache.Items[Index].Mask;
    }
  }

  INT32  Side = (INT32)CeilF (Size + 2 * IconPad (Size)) + 1;
  MASK  *Mask;
  RASTER Raster;

  if (!RasterStart (Side, Side, &Raster, &Mask)) {
    return NULL;
  }
  PEN Pen = {.Raster = &Raster,
             .Ox     = IconPad (Size),
             .Oy     = IconPad (Size),
             .Scale  = Size / 24.0f};
  Shape (&Pen, Icon);
  RasterFinish (&Raster, Mask);
  // A mask that can't be cached would never be freed, so drop it.
  if (!VecPush (&mCache, (CACHED){.Icon = Icon, .Key = Key, .Mask = Mask})) {
    FreePool (Mask);
    return NULL;
  }
  return Mask;
}

VOID
IconsReset (VOID)
{
  for (UINTN Index = 0; Index < mCache.Count; Index++) {
    FreePool (mCache.Items[Index].Mask);
  }
  mCache.Count = 0;
}
