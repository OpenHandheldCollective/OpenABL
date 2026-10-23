/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Fat32Make - writes a new FAT32 volume holding a given set of files.

  The installer formats PortareOS's boot partition with it. Fat32MakePlan
  gives every directory and file one run of clusters, the caller writes each
  file's data at Fat32MakeOffset, then Fat32MakeWrite writes the FATs, the
  directories and, last, the boot sectors, so the volume only mounts once
  the rest is on disk. Writes go through a callback so host tests can use an
  image.
*/

#ifndef __CORE_FAT32_MAKE_H__
#define __CORE_FAT32_MAKE_H__

#include "Base/OablBase.h"

// Writes Size bytes at byte Offset of the volume. FALSE on an I/O error.
typedef BOOLEAN
FAT32_WRITE (
  IN UINT64       Offset,
  IN CONST UINT8 *Buffer,
  IN UINTN        Size,
  IN VOID        *Context
);

// A directory or file for the new volume. A directory comes before what it
// holds.
typedef struct _FAT32_NODE {
  // NUL-terminated UTF-16, at most 255 units.
  CONST CHAR16 *Name;
  // A file's size, under 4 GiB; 0 for a directory.
  UINT64 Size;
  // The index of the directory holding it, or -1 for the root.
  INTN    Parent;
  BOOLEAN Dir;
  // Last change, as FAT date and time.
  UINT16 Date;
  UINT16 Time;
  // Set by Fat32MakePlan.
  UINT32  Cluster;  // first cluster; 0 for an empty file
  UINT32  Clusters; // how many, one run
  UINT8   Short[11];
  BOOLEAN Long; // has long-name entries
} FAT32_NODE;

typedef struct _FAT32_PLAN {
  // Set by the caller.
  UINT64      Bytes;         // volume size
  UINT32      SectorSize;    // the disk's block size, 512 to 4096
  UINT32      HiddenSectors; // where the volume starts on its disk
  UINT32      Serial;
  CHAR8       Label[12];     // up to 11 characters
  UINT16      Date;          // for the label
  UINT16      Time;
  FAT32_NODE *Nodes;
  UINTN       Count;
  // Set by Fat32MakePlan.
  UINT32 Reserved;     // sectors before the first FAT
  UINT32 FatSectors;   // sectors per FAT
  UINT32 ClusterSize;  // bytes
  UINT32 Clusters;     // data clusters
  UINT32 Used;         // clusters in use, from cluster 2
  UINT32 RootClusters; // the root directory's, from cluster 2
} FAT32_PLAN;

// Lays out the volume and its nodes. FALSE if the volume is too small or
// too large for FAT32, a name is empty, too long or taken twice in one
// directory (ignoring case), a parent isn't an earlier directory, a file is
// 4 GiB or more, or it all doesn't fit.
BOOLEAN
Fat32MakePlan (IN OUT FAT32_PLAN *Plan);

// Returns the byte offset of Cluster in the volume.
UINT64
Fat32MakeOffset (
  IN CONST FAT32_PLAN *Plan,
  IN UINT32            Cluster
);

// Writes everything but the files' data: both FATs, the directories, then
// the boot sectors and FS information sectors with their backups. Doesn't
// write the free clusters.
BOOLEAN
Fat32MakeWrite (
  IN CONST FAT32_PLAN *Plan,
  IN FAT32_WRITE      *Write,
  IN VOID             *Context
);

#endif
