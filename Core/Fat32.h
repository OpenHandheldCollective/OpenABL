/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Fat32 - Read-only FAT32 reader.

  Used when XBL's FAT driver can't read a boot file. Only 8.3 names are
  supported. Reads go through a callback so host tests can use an image.
*/

#ifndef __CORE_FAT32_H__
#define __CORE_FAT32_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"

// Reads Size bytes at byte Offset of the volume. FALSE on an I/O error.
typedef BOOLEAN
FAT32_READ (
  IN UINT64  Offset,
  OUT UINT8 *Buffer,
  IN UINTN   Size,
  IN VOID   *Context
);

typedef struct _FAT32_FILE {
  UINT32 Cluster;
  UINT32 Size;
} FAT32_FILE;

typedef struct _FAT32_VOLUME {
  FAT32_READ *Read;
  VOID       *Ctx;
  UINT32      BlockSize;
  UINT32      SectorsPerCluster;
  UINT32      ClusterBytes;
  UINT64      FatStart;
  UINT64      DataStart;
  UINT32      Root;
  UINT32      MaxCluster;
  // Volume label from the boot sector, padded with spaces.
  UINT8 Label[11];
  // Cache of the last FAT sector read.
  BOOLEAN FatCached;
  UINT64  FatSector;
  UINT8  *FatCache;
} FAT32_VOLUME;

// Mounts a FAT32 volume. Fails on FAT12, FAT16 and anything else.
BOOLEAN
Fat32Mount (
  OUT FAT32_VOLUME *V,
  IN UINT32         BlockSize,
  IN FAT32_READ    *Read,
  IN VOID          *Context
);

// Frees the FAT sector cache.
VOID
Fat32Free (IN OUT FAT32_VOLUME *V);

// Finds a file by backslash-separated Path. Fails for directories.
BOOLEAN
Fat32Find (
  IN OUT FAT32_VOLUME *V,
  IN CONST CHAR8      *Path,
  OUT FAT32_FILE      *File
);

// Reads up to Length bytes from Offset into Out. Caller frees Out.
BOOLEAN
Fat32Read (
  IN OUT FAT32_VOLUME *V,
  IN CONST FAT32_FILE *File,
  IN UINT64            Offset,
  IN UINTN             Length,
  OUT OABL_BUFFER     *Out
);

#endif
