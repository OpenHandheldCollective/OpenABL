/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present PortareOS
 *   https://github.com/portare-ch
 */

/**
  Vga - the IBM VGA 8x16 font in CP437 order, as portarelauncher draws it.
  One byte a row, the leftmost pixel in the top bit.
*/

#ifndef __VIEW_VGA_H__
#define __VIEW_VGA_H__

#include "Base/OablBase.h"

#define VGA_W 8
#define VGA_H 16

extern CONST UINT8 gVga[256][VGA_H];

#endif
