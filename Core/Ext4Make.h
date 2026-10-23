/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Ext4Make - writes a new, empty ext4 file system.

  The installer formats PortareOS's storage partition with it. The result
  is what mke2fs makes for a flash device with lazy initialisation: 4 KiB
  blocks, extents, flex_bg, 64bit and metadata_csum, with an internal
  journal and no resize inode. Only the metadata in use is written; the
  kernel zeroes the rest of the inode tables in the background on first
  mount. The primary superblock goes last, so an interrupted run doesn't
  leave a file system that mounts. Writes go through a callback so host
  tests can use an image.
*/

#ifndef __CORE_EXT4_MAKE_H__
#define __CORE_EXT4_MAKE_H__

#include "Base/OablBase.h"

// Writes Size bytes at byte Offset of the partition. FALSE on an I/O error.
typedef BOOLEAN
EXT4_WRITE (
  IN UINT64       Offset,
  IN CONST UINT8 *Buffer,
  IN UINTN        Size,
  IN VOID        *Context
);

typedef struct _EXT4_MAKE {
  UINT64 Bytes;         // partition size
  UINT32 InodeRatio;    // bytes per inode, a power of two from 16 KiB
  UINT32 JournalBlocks; // 4 KiB blocks, at most 131072
  UINT8  Uuid[16];
  UINT8  HashSeed[16];  // for directory hashes; random
  CHAR8  Label[17];     // up to 16 characters
  UINT32 Time;          // seconds since 1970, for the timestamps
} EXT4_MAKE;

// Formats the partition. FALSE if it's under 64 MiB or over 16 TiB, the
// parameters are out of range, or a write fails.
BOOLEAN
Ext4Make (
  IN CONST EXT4_MAKE *Params,
  IN EXT4_WRITE      *Write,
  IN VOID            *Context
);

#endif
