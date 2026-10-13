/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Bmp - Windows bitmap decoder for splash images.
*/

#include "Core/Bmp.h"

#include "Base/OablBase.h"

#include "Base/Memory.h"
#include "Base/Buffer.h"

UINT32 *
BmpDecode (
  IN CONST UINT8 *Data,
  IN UINTN        Size,
  OUT UINTN      *Width,
  OUT UINTN      *Height
)
{
  UINT32  PixelOffset;
  UINT32  Compression;
  UINT32 *Out;
  INT32   W;
  INT32   H;
  UINT16  BitsPerPixel;
  UINTN   Rows;
  UINTN   BytesPerPixel;
  UINTN   RowSize;
  BOOLEAN TopDown;

  if (Size < 54 || Data[0] != 'B' || Data[1] != 'M') {
    return NULL;
  }
  PixelOffset  = ReadLe32 (Data + 10);
  W            = (INT32)ReadLe32 (Data + 18);
  H            = (INT32)ReadLe32 (Data + 22);
  BitsPerPixel = ReadLe16 (Data + 28);
  Compression  = ReadLe32 (Data + 30);
  // BI_RGB, or BI_BITFIELDS, which 32-bit writers often use for X8R8G8B8.
  // The masks are assumed to be the usual ones.
  if (!(Compression == 0 || (Compression == 3 && BitsPerPixel == 32)) ||
      !(BitsPerPixel == 24 || BitsPerPixel == 32) || W <= 0 || H == 0) {
    return NULL;
  }
  // A negative height means a top-down image.
  Rows    = H < 0 ? (UINTN)-(INT64)H : (UINTN)H;
  TopDown = H < 0;
  if (W > 8192 || Rows > 8192) {
    return NULL;
  }
  BytesPerPixel = BitsPerPixel / 8u;
  // Rows are padded to 4 bytes.
  RowSize = ((UINTN)W * BytesPerPixel + 3) / 4 * 4;
  if ((UINT64)PixelOffset + (UINT64)RowSize * Rows > Size) {
    return NULL;
  }
  Out = AllocatePool ((UINTN)W * Rows * sizeof (*Out));
  if (!Out) {
    return NULL;
  }
  for (UINTN Y = 0; Y < Rows; Y++) {
    UINTN        SourceRow = TopDown ? Y : Rows - 1 - Y;
    CONST UINT8 *Line      = Data + PixelOffset + SourceRow * RowSize;

    for (UINTN X = 0; X < (UINTN)W; X++) {
      CONST UINT8 *Pixel = Line + X * BytesPerPixel;
      Out[Y * (UINTN)W + X] =
        (UINT32)Pixel[2] << 16 | (UINT32)Pixel[1] << 8 | Pixel[0];
    }
  }
  *Width  = (UINTN)W;
  *Height = Rows;
  return Out;
}
