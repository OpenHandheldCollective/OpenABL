/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Bmp - Windows bitmap decoder for splash images.

  Handles uncompressed 24- and 32-bit images, bottom-up or top-down, and
  decodes them to 0x00RRGGBB pixels.
*/

#ifndef __CORE_BMP_H__
#define __CORE_BMP_H__

#include "Base/OablBase.h"

// Decodes a BMP to top-down pixels. Returns NULL if the format isn't
// supported. Caller frees the result.
UINT32 *
BmpDecode (
  IN CONST UINT8 *Data,
  IN UINTN        Size,
  OUT UINTN      *Width,
  OUT UINTN      *Height
);

#endif
