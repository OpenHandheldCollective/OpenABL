/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  string - <string.h> for the UEFI build, which has no C library.

  Only what microui uses. Rt/Rt.c has the memory functions and
  View/Libc.c strlen.
*/

#ifndef OABL_STRING_H
#define OABL_STRING_H

#include <stddef.h>

void *
memcpy (
  void       *Dst,
  const void *Src,
  size_t      N
);

void *
memset (
  void  *Dst,
  int    C,
  size_t N
);

size_t
strlen (const char *String);

#endif
