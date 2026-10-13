/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SysFontGlyphs - Scaled system fonts for an XBL without them.

  The SM8750 XBL has no FontDxe, so the ABL's own screens (unlock prompt,
  verified-boot warnings) use the tiny 8x19 base font. Efi/SysFont.c
  registers sysfont2x to sysfont8x from these packages instead.
*/

#ifndef __CORE_SYS_FONT_GLYPHS_H__
#define __CORE_SYS_FONT_GLYPHS_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"

#define SYSFONT_WIDTH  8
#define SYSFONT_HEIGHT 19
#define SYSFONT_FIRST  0x20
#define SYSFONT_COUNT  95
// Registered as sysfont{N}x, limits included.
#define SYSFONT_SCALE_MIN 2
#define SYSFONT_SCALE_MAX 8

// EDK2's 8x19 narrow font. One byte per row, leftmost pixel in the top bit.
extern CONST UINT8 gSysFontGlyphs[SYSFONT_COUNT * SYSFONT_HEIGHT];

// Scales the glyphs by Scale, at most 8. Caller frees Out.
BOOLEAN
SysFontScaled (
  IN UINT8         Scale,
  OUT OABL_BUFFER *Out
);

// Builds the HII package list for sysfont{Scale}x, laid out like the ABL's,
// with GUID 46c1220e-be05-4375-8be6-2396ae7b42NN where NN is the scale.
// Caller frees Out.
BOOLEAN
SysFontPackageList (
  IN UINT8         Scale,
  OUT OABL_BUFFER *Out
);

#endif
