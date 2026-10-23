/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Test - checks for the host tests.
*/

#ifndef __TESTS_TEST_H__
#define __TESTS_TEST_H__

#include <stdio.h>

#include "Base/OablBase.h"

// Failed checks so far.
extern INT32 gTestFailures;

#define CHECK(Cond)                                                \
  do {                                                             \
    if (!(Cond)) {                                                 \
      fprintf (stderr, "%s:%d: %s\n", __FILE__, __LINE__, #Cond);  \
      gTestFailures++;                                             \
    }                                                              \
  } while (0)

// The install layouts in Core/Gpt. Returns the failures.
INT32
GptTests (VOID);

#endif
