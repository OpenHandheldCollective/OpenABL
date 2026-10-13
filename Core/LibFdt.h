/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */

/**
  LibFdt - The libfdt functions used by Core/DeviceTree.c.

  The declarations come from the ABL's EmbeddedPkg/Include/libfdt.h and
  fdt.h. At run time the ABL's FdtLib provides the functions through
  OPENABL_ABL_LIB_PROTOCOL (Efi/Abl.c). The libfdt licence follows.

  libfdt - Flat Device Tree manipulation
  Copyright (C) 2006 David Gibson, IBM Corporation.
  Copyright 2012 Kim Phillips, Freescale Semiconductor.

  libfdt is dual licensed: you can use it either under the terms of
  the GPL, or the BSD license, at your option.

   a) This library is free software; you can redistribute it and/or
      modify it under the terms of the GNU General Public License as
      published by the Free Software Foundation; either version 2 of the
      License, or (at your option) any later version.

      This library is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
      GNU General Public License for more details.

      You should have received a copy of the GNU General Public
      License along with this library; if not, write to the Free
      Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
      MA 02110-1301 USA

  Alternatively,

   b) Redistribution and use in source and binary forms, with or
      without modification, are permitted provided that the following
      conditions are met:

      1. Redistributions of source code must retain the above
         copyright notice, this list of conditions and the following
         disclaimer.
      2. Redistributions in binary form must reproduce the above
         copyright notice, this list of conditions and the following
         disclaimer in the documentation and/or other materials
         provided with the distribution.

      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
      CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
      INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
      MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
      DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
      CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
      SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
      NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
      LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
      HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
      CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
      OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
      EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __CORE_LIB_FDT_H__
#define __CORE_LIB_FDT_H__

#include <stdint.h>

// fdt.h

typedef uint32_t fdt32_t;

struct fdt_header {
  fdt32_t Magic;           // magic word FDT_MAGIC
  fdt32_t Totalsize;       // total size of DT block
  fdt32_t OffDtStruct;     // offset to structure
  fdt32_t OffDtStrings;    // offset to strings
  fdt32_t OffMemRsvmap;    // offset to memory reserve map
  fdt32_t Version;         // format version
  fdt32_t LastCompVersion; // last compatible version

  // version 2 fields below
  fdt32_t BootCpuidPhys; // which physical CPU id we're booting on
  // version 3 fields below
  fdt32_t SizeDtStrings; // size of the strings block

  // version 17 fields below
  fdt32_t SizeDtStruct; // size of the structure block
};

#define FDT_MAGIC 0xd00dfeed // 4: version, 4: total size

// Start node: full name.
#define FDT_BEGIN_NODE 0x1
// End node.
#define FDT_END_NODE 0x2
// Property: name offset, size, content.
#define FDT_PROP 0x3
// No operation.
#define FDT_NOP 0x4
#define FDT_END 0x9

// libfdt_env.h. The blob is big-endian.

static inline uint32_t
fdt32_to_cpu (fdt32_t X)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return X;
#else
  return __builtin_bswap32 (X);
#endif
}

// libfdt.h

// Error codes. Functions return them negated.

// The requested node or property does not exist.
#define FDT_ERR_NOTFOUND 1
// Attempted to create a node or property which already exists.
#define FDT_ERR_EXISTS 2
// Operation needed to expand the device tree, but its buffer did not have
// sufficient space to contain the expanded tree. Use fdt_open_into() to move
// the device tree to a buffer with more space.
#define FDT_ERR_NOSPACE 3
// Function was passed an incomplete device tree created by the
// sequential-write functions, which is not sufficiently complete for the
// requested operation.
#define FDT_ERR_BADSTATE 7

#define fdt_get_header(fdt, field)                                             \
  (fdt32_to_cpu (((const struct fdt_header *)(fdt))->field))
#define fdt_magic(fdt)           (fdt_get_header (fdt, Magic))
#define fdt_totalsize(fdt)       (fdt_get_header (fdt, Totalsize))
#define fdt_off_dt_struct(fdt)   (fdt_get_header (fdt, OffDtStruct))
#define fdt_off_dt_strings(fdt)  (fdt_get_header (fdt, OffDtStrings))
#define fdt_version(fdt)         (fdt_get_header (fdt, Version))
#define fdt_size_dt_strings(fdt) (fdt_get_header (fdt, SizeDtStrings))
#define fdt_size_dt_struct(fdt)  (fdt_get_header (fdt, SizeDtStruct))

// Returns the node at Path. A path without a leading '/' is an alias. A
// component without a unit address matches a node that has one.
int
fdt_path_offset (
  const void *Fdt,
  const char *Path
);

// Returns the node's name. Lenp gets its length, or the error.
const char *
fdt_get_name (
  const void *Fdt,
  int         Nodeoffset,
  int        *Lenp
);

// Returns the value of property Name. Lenp gets its length, or the error.
const void *
fdt_getprop (
  const void *Fdt,
  int         Nodeoffset,
  const char *Name,
  int        *Lenp
);

// Returns the node after Offset, or the first if Offset is negative. Depth,
// if not NULL, follows the nesting level.
int
fdt_next_node (
  const void *Fdt,
  int         Offset,
  int        *Depth
);

int
fdt_first_subnode (
  const void *Fdt,
  int         Offset
);

int
fdt_next_subnode (
  const void *Fdt,
  int         Offset
);

// Pass Startoffset -1 to search from the start.
int
fdt_node_offset_by_compatible (
  const void *Fdt,
  int         Startoffset,
  const char *Compatible
);

// A new property goes first in the node. The blob grows within totalsize.
int
fdt_setprop (
  void       *Fdt,
  int         Nodeoffset,
  const char *Name,
  const void *Val,
  int         Len
);

// Adds an empty subnode after the properties, or returns -FDT_ERR_EXISTS.
int
fdt_add_subnode (
  void       *Fdt,
  int         Parentoffset,
  const char *Name
);

#endif
