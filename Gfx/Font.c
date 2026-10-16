/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Font - the menu font at any size, with fallback faces for missing
  characters (the CJK glyphs for the Chinese strings).

  .oablf format, little-endian:
    "OABF", u16 version (1), u16 units per em, i16 ascender, i16 descender,
    i16 cap height, u16 character count N, u16 weight count W
    N x u16 code points, sorted
    W x N glyph records: u16 advance, i16 x0 y0 x1 y1, u32 outline offset.
      An advance of 0xffff means the weight uses the first weight's glyph.
    Outlines: an op byte (0 move, 1 line, 2 quadratic, 3 cubic, 4 close,
      anything else ends the glyph) and its points. Each point is a zigzag
      LEB128 delta from the previous one, in font units.
*/

#include "Gfx/Gfx.h"
#include "Base/Memory.h"

#define RECORD    14
#define ABSENT    0xffff
#define MAX_FONTS 48
#define MAX_FACES 2

typedef struct _FACE {
  CONST UINT8 *Data;
  UINTN        Size;
  UINT32       Chars, Weights;
  UINTN        CharsAt, RecordsAt, OutlinesAt;
  float        UnitsPerEm;
  INT16        Ascender, Descender, CapHeight;
} FACE;

// The font, then its fallbacks. The first face gives the metrics.
STATIC FACE  mFaces[MAX_FACES];
STATIC UINTN mFaceCount;

// One size and weight. Glyph slots are allocated per face on first use.
typedef struct _FONT {
  UINT32      Key;
  float       Size;
  FONT_WEIGHT Weight;
  FONT_GLYPH *Glyphs[MAX_FACES];
} FONT;

STATIC FONT  mFonts[MAX_FONTS];
STATIC UINTN mFontCount;

STATIC UINT16
U16At (
  IN CONST FACE *Face,
  IN UINTN       Offset
)
{
  return (UINT16)(Face->Data[Offset] | Face->Data[Offset + 1] << 8);
}

STATIC INT16
I16At (
  IN CONST FACE *Face,
  IN UINTN       Offset
)
{
  return (INT16)U16At (Face, Offset);
}

// Fails if Data isn't a .oablf file.
STATIC BOOLEAN
Parse (
  IN CONST UINT8 *Data,
  IN UINTN        Size,
  OUT FACE       *Face
)
{
  if (Size < 18 || Data[0] != 'O' || Data[1] != 'A' || Data[2] != 'B' ||
      Data[3] != 'F') {
    return FALSE;
  }
  *Face = (FACE){.Data = Data, .Size = Size};
  if (U16At (Face, 4) != 1) {
    return FALSE;
  }
  Face->UnitsPerEm = U16At (Face, 6);
  Face->Ascender   = I16At (Face, 8);
  Face->Descender  = I16At (Face, 10);
  Face->CapHeight  = I16At (Face, 12);
  Face->Chars      = U16At (Face, 14);
  Face->Weights    = U16At (Face, 16);
  Face->CharsAt    = 18;
  Face->RecordsAt  = Face->CharsAt + Face->Chars * 2;
  Face->OutlinesAt =
    Face->RecordsAt + (UINTN)Face->Weights * Face->Chars * RECORD;
  return Face->Weights && Face->UnitsPerEm > 0 && Size >= Face->OutlinesAt;
}

BOOLEAN
FontLoad (
  IN CONST UINT8 *Data,
  IN UINTN        Size
)
{
  mFaceCount = Parse (Data, Size, &mFaces[0]) ? 1 : 0;
  return mFaceCount == 1;
}

BOOLEAN
FontAddFallback (
  IN CONST UINT8 *Data,
  IN UINTN        Size
)
{
  if (mFaceCount == 0 || mFaceCount == MAX_FACES ||
      !Parse (Data, Size, &mFaces[mFaceCount])) {
    return FALSE;
  }
  mFaceCount++;
  return TRUE;
}

// Returns the index of CodePoint in Face, or -1.
STATIC INT32
IndexOf (
  IN CONST FACE *Face,
  IN UINT32      CodePoint
)
{
  UINT32 Low  = 0;
  UINT32 High = Face->Chars;

  while (Low < High) {
    UINT32 Mid   = (Low + High) / 2;
    UINT16 Entry = U16At (Face, Face->CharsAt + Mid * 2);
    if (Entry == CodePoint) {
      return (INT32)Mid;
    }
    if (Entry < CodePoint) {
      Low = Mid + 1;
    } else {
      High = Mid;
    }
  }
  return -1;
}

STATIC UINTN
Record (
  IN CONST FACE *Face,
  IN UINT32      Weight,
  IN UINT32      Index
)
{
  return Face->RecordsAt + ((UINTN)Weight * Face->Chars + Index) * RECORD;
}

// Uses Weight's own table if it has the glyph, else the first weight's.
STATIC UINT32
WeightOf (
  IN CONST FACE *Face,
  IN FONT_WEIGHT Weight,
  IN UINT32      Index
)
{
  UINT32 Want =
    (UINT32)Weight < Face->Weights ? (UINT32)Weight : Face->Weights - 1;
  return Want > 0 && U16At (Face, Record (Face, Want, Index)) != ABSENT ? Want
                                                                        : 0;
}

STATIC FONT *
FontFor (
  IN float       Size,
  IN FONT_WEIGHT Weight
)
{
  UINT32 Key = (UINT32)(Size * 4.0f + 0.5f) | (UINT32)Weight << 24;

  for (UINTN Index = 0; Index < mFontCount; Index++) {
    if (mFonts[Index].Key == Key) {
      return &mFonts[Index];
    }
  }
  // When out of slots, free the last font's glyphs and reuse it.
  FONT *Font = &mFonts[mFontCount < MAX_FONTS ? mFontCount++ : MAX_FONTS - 1];
  for (UINTN Face = 0; Face < MAX_FACES; Face++) {
    if (Font->Glyphs[Face]) {
      for (UINT32 Index = 0; Index < mFaces[Face].Chars; Index++) {
        FreePool (Font->Glyphs[Face][Index].Mask);
      }
      FreePool (Font->Glyphs[Face]);
    }
  }
  *Font = (FONT){.Key = Key, .Size = Size, .Weight = Weight};
  return Font;
}

// Sets the glyph's advance and box at the font's size. The box is rounded
// outwards, with y running down from the baseline.
STATIC VOID
Measure (
  IN CONST FACE  *Face,
  IN CONST FONT  *Font,
  IN UINT32       Index,
  OUT FONT_GLYPH *Glyph
)
{
  UINTN At    = Record (Face, WeightOf (Face, Font->Weight, Index), Index);
  float Scale = Font->Size / Face->UnitsPerEm;
  INT16 X0    = I16At (Face, At + 2);
  INT16 Y0    = I16At (Face, At + 4);
  INT16 X1    = I16At (Face, At + 6);
  INT16 Y1    = I16At (Face, At + 8);

  Glyph->Measured = TRUE;
  Glyph->Advance  = (float)U16At (Face, At) * Scale;
  if (X0 >= X1 || Y0 >= Y1) {
    return;
  }
  float MinX  = FloorF (X0 * Scale);
  float MinY  = FloorF (-Y1 * Scale);
  Glyph->Left = (INT16)MinX;
  Glyph->Top  = (INT16)MinY;
  Glyph->W    = (UINT16)(CeilF (X1 * Scale) - MinX);
  Glyph->H    = (UINT16)(CeilF (-Y0 * Scale) - MinY);
}

// Read position in the outlines, and the current point.
typedef struct _CURSOR {
  UINTN Offset;
  INT32 X, Y;
} CURSOR;

// Reads the next point, two zigzag LEB128 deltas from the current one.
STATIC VOID
NextPoint (
  IN CONST FACE *Face,
  IN OUT CURSOR *Cursor
)
{
  for (INT32 Axis = 0; Axis < 2; Axis++) {
    UINT32 Zigzag = 0;
    UINT32 Shift  = 0;
    while (Cursor->Offset < Face->Size) {
      UINT8 Byte = Face->Data[Cursor->Offset++];
      Zigzag |= (UINT32)(Byte & 0x7f) << Shift;
      Shift += 7;
      if (!(Byte & 0x80)) {
        break;
      }
    }
    INT32 Delta = (INT32)(Zigzag >> 1) ^ -(INT32)(Zigzag & 1);
    if (Axis == 0) {
      Cursor->X += Delta;
    } else {
      Cursor->Y += Delta;
    }
  }
}

// Rasterises glyph Index. Glyph->Mask stays NULL if memory runs out.
STATIC VOID
Draw (
  IN CONST FACE     *Face,
  IN CONST FONT     *Font,
  IN UINT32          Index,
  IN OUT FONT_GLYPH *Glyph
)
{
  UINTN   At     = Record (Face, WeightOf (Face, Font->Weight, Index), Index);
  float   Scale  = Font->Size / Face->UnitsPerEm;
  RASTER  Raster = {.W = Glyph->W, .H = Glyph->H};
  CURSOR  Cursor = {.Offset =
                      Face->OutlinesAt + (U16At (Face, At + 10) |
                                          (UINT32)U16At (Face, At + 12) << 16)};
  float   LastX  = 0;
  float   LastY  = 0;
  float   StartX = 0;
  float   StartY = 0;
  BOOLEAN Open   = FALSE;

  Raster.Cells =
    AllocateZeroArray (RasterCells (Glyph->W, Glyph->H), sizeof (float));
  Glyph->Mask = AllocatePool ((UINTN)Glyph->W * Glyph->H);
  if (!Raster.Cells || !Glyph->Mask) {
    FreePool (Raster.Cells);
    FreePool (Glyph->Mask);
    Glyph->Mask = NULL;
    return;
  }
// The cursor's point, in pixels within the glyph box.
#define PX(Point) ((float)(Point).X * Scale - Glyph->Left)
#define PY(Point) ((float)-(Point).Y * Scale - Glyph->Top)
  while (Cursor.Offset < Face->Size) {
    UINT8 Op = Face->Data[Cursor.Offset++];
    if (Op == 0) {
      NextPoint (Face, &Cursor);
      LastX = StartX = PX (Cursor);
      LastY = StartY = PY (Cursor);
      Open           = TRUE;
    } else if (Op == 1) {
      NextPoint (Face, &Cursor);
      RasterLine (&Raster, LastX, LastY, PX (Cursor), PY (Cursor));
      LastX = PX (Cursor), LastY = PY (Cursor);
    } else if (Op == 2) {
      NextPoint (Face, &Cursor);
      float X1 = PX (Cursor);
      float Y1 = PY (Cursor);
      NextPoint (Face, &Cursor);
      RasterQuad (&Raster, LastX, LastY, X1, Y1, PX (Cursor), PY (Cursor));
      LastX = PX (Cursor), LastY = PY (Cursor);
    } else if (Op == 3) {
      NextPoint (Face, &Cursor);
      float X1 = PX (Cursor);
      float Y1 = PY (Cursor);
      NextPoint (Face, &Cursor);
      float X2 = PX (Cursor);
      float Y2 = PY (Cursor);
      NextPoint (Face, &Cursor);
      RasterCubic (
        &Raster, LastX, LastY, X1, Y1, X2, Y2, PX (Cursor), PY (Cursor)
      );
      LastX = PX (Cursor), LastY = PY (Cursor);
    } else {
      // An explicit close, or the implicit one at the end.
      if (Open) {
        RasterLine (&Raster, LastX, LastY, StartX, StartY);
      }
      Open = FALSE;
      if (Op != 4) {
        break;
      }
    }
  }
#undef PX
#undef PY
  RasterMask (&Raster, Glyph->Mask, Glyph->W);
  FreePool (Raster.Cells);
}

CONST FONT_GLYPH *
FontGlyph (
  IN float       Size,
  IN FONT_WEIGHT Weight,
  IN UINT32      CodePoint,
  IN BOOLEAN     WithMask
)
{
  UINTN       Face  = 0;
  INT32       Index = -1;
  FONT       *Font;
  FONT_GLYPH *Glyph;

  // Use the first face that has it, else '?' from the main font.
  while (Face < mFaceCount &&
         (Index = IndexOf (&mFaces[Face], CodePoint)) < 0) {
    Face++;
  }
  if (Index < 0 && mFaceCount) {
    Face  = 0;
    Index = IndexOf (&mFaces[0], '?');
  }
  if (Index < 0) {
    return NULL;
  }
  Font = FontFor (Size, Weight);
  if (!Font->Glyphs[Face]) {
    Font->Glyphs[Face] =
      AllocateZeroArray (mFaces[Face].Chars, sizeof (FONT_GLYPH));
    if (!Font->Glyphs[Face]) {
      return NULL;
    }
  }
  Glyph = &Font->Glyphs[Face][Index];
  if (!Glyph->Measured) {
    Measure (&mFaces[Face], Font, (UINT32)Index, Glyph);
  }
  if (WithMask && !Glyph->Mask && Glyph->W && Glyph->H) {
    Draw (&mFaces[Face], Font, (UINT32)Index, Glyph);
  }
  return Glyph;
}

float
FontCapHeight (IN float Size)
{
  return mFaceCount ? Size * mFaces[0].CapHeight / mFaces[0].UnitsPerEm
                    : Size * 0.7f;
}

float
FontAscender (IN float Size)
{
  return mFaceCount ? Size * mFaces[0].Ascender / mFaces[0].UnitsPerEm : Size;
}

float
FontDescender (IN float Size)
{
  return mFaceCount ? Size * mFaces[0].Descender / mFaces[0].UnitsPerEm
                    : -Size / 4;
}

UINT32
Utf8Next (
  IN CONST CHAR8 *String,
  OUT UINTN      *Length
)
{
  CONST UINT8 *Bytes = (CONST UINT8 *)String;

  if (Bytes[0] < 0x80 || !Bytes[1]) {
    *Length = 1;
    return Bytes[0];
  }
  if ((Bytes[0] & 0xe0) == 0xc0) {
    *Length = 2;
    return (UINT32)(Bytes[0] & 0x1f) << 6 | (Bytes[1] & 0x3f);
  }
  if ((Bytes[0] & 0xf0) == 0xe0 && Bytes[2]) {
    *Length = 3;
    return (UINT32)(Bytes[0] & 0x0f) << 12 | (UINT32)(Bytes[1] & 0x3f) << 6 |
           (Bytes[2] & 0x3f);
  }
  *Length = 1;
  return '?';
}
