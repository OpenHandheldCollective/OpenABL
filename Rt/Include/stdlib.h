/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  stdlib - <stdlib.h> for the UEFI build, which has no C library.

  Only what microui uses. View/Libc.c implements it.
*/

#ifndef OABL_STDLIB_H
#define OABL_STDLIB_H

#include <stddef.h>

void
qsort (
  void  *Base,
  size_t Count,
  size_t Size,
  int (*Compare) (
    const void *,
    const void *
  )
);

double
strtod (
  const char *String,
  char      **End
);

void
abort (void);

#endif
