/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SysFont - registers XBL's scaled system fonts when XBL has none.

  Qualcomm's screens draw with sysfont2x to sysfont8x, which SM8750's XBL
  lacks. They are added to the HII database once a boot, before those
  screens draw. Built only with OABL_SYSFONT.
*/

#ifndef __EFI_SYS_FONT_H__
#define __EFI_SYS_FONT_H__

#include "Efi/Efi.h"

#include <Protocol/OpenAblMenuHost.h>

#ifdef OABL_SYSFONT
// Registers one HII package list per scale, unless a font named
// "sysfont2x" already exists. Logs to the ABL's logger; Log may be NULL.
// Does nothing without the HII protocols.
VOID
SysFontEnsure (IN OPENABL_STORAGE_LOG Log OPTIONAL);
#endif

#endif
