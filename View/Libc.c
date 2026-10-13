/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Libc - the C library functions microui calls, for the UEFI build.

  The menu sorts at most a few windows and never reaches microui's number
  fields or its failed assertions, so these are minimal. The simulator
  uses the host's C library.
*/

#ifdef OABL_UEFI

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t
strlen (const char *String)
{
  size_t Length = 0;

  while (String[Length]) {
    Length++;
  }
  return Length;
}

// Insertion sort, for elements up to 64 bytes.
void
qsort (
  void  *Base,
  size_t Count,
  size_t Size,
  int (*Compare) (
    const void *,
    const void *
  )
)
{
  unsigned char *Items = Base;
  unsigned char  Swap[64];

  if (Size > sizeof (Swap)) {
    return;
  }
  for (size_t Index = 1; Index < Count; Index++) {
    for (size_t At = Index;
         At > 0 && Compare (Items + (At - 1) * Size, Items + At * Size) > 0;
         At--) {
      memcpy (Swap, Items + At * Size, Size);
      memcpy (Items + At * Size, Items + (At - 1) * Size, Size);
      memcpy (Items + (At - 1) * Size, Swap, Size);
    }
  }
}

double
strtod (
  const char *String,
  char      **End
)
{
  if (End) {
    *End = (char *)String;
  }
  return 0;
}

FILE *stderr;

int
fprintf (
  FILE       *File,
  const char *Format,
  ...
)
{
  (void)File, (void)Format;
  return 0;
}

int
sprintf (
  char       *Out,
  const char *Format,
  ...
)
{
  (void)Format;
  Out[0] = 0;
  return 0;
}

void
abort (void)
{
  for (;;) {
  }
}

#endif
