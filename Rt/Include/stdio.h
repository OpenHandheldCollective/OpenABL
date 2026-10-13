/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  stdio - <stdio.h> for the UEFI build, which has no C library.

  Only what microui uses. View/Libc.c implements it.
*/

#ifndef OABL_STDIO_H
#define OABL_STDIO_H

typedef struct _OABL_FILE FILE;

extern FILE *stderr;

int
fprintf (
  FILE       *File,
  const char *Format,
  ...
);

int
sprintf (
  char       *Out,
  const char *Format,
  ...
);

#endif
