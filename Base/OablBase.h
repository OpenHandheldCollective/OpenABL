/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  OablBase - edk2's base types for code built for both UEFI and the host.

  The UEFI build gets them from edk2's Base.h. The host build (the
  simulator) defines them here.
*/

#ifndef __BASE_OABL_BASE_H__
#define __BASE_OABL_BASE_H__

#include <stddef.h>

#ifdef OABL_UEFI

#include <Base.h>

#else

typedef unsigned char UINT8;
typedef signed char INT8;
typedef unsigned short UINT16;
typedef short INT16;
typedef unsigned int UINT32;
typedef int INT32;
typedef unsigned long long UINT64;
typedef long long INT64;
typedef size_t UINTN;
typedef ptrdiff_t INTN;
typedef char CHAR8;
typedef unsigned short CHAR16;
typedef unsigned char BOOLEAN;

#define VOID   void
#define CONST  const
#define STATIC static
#define IN
#define OUT
#define OPTIONAL

#define TRUE  ((BOOLEAN)(1 == 1))
#define FALSE ((BOOLEAN)(0 == 1))

#define MAX_UINT32 ((UINT32)0xFFFFFFFF)
#define MAX_UINT64 ((UINT64)0xFFFFFFFFFFFFFFFFULL)
#define MAX_UINTN  ((UINTN)-1)

#endif

#endif
