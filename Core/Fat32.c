/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Fat32 - Read-only FAT32 reader.
*/

#include "Core/Fat32.h"

#include "Base/Memory.h"
#include "Base/Text.h"

// Values from here up (end of chain, bad cluster, reserved) end a walk.
#define CHAIN_END   0x0FFFFFF8u
#define ATTR_LFN    0x0F
#define ATTR_VOLUME 0x08
#define ATTR_DIR    0x10

STATIC UINT8
Upper (IN CHAR8 C)
{
  return (UINT8)(C >= 'a' && C <= 'z' ? C - 32 : C);
}

// Converts a path component to the 11-byte name of an 8.3 directory entry.
STATIC VOID
ShortName (
  IN OABL_TEXT Component,
  OUT UINT8    Name[11]
)
{
  OABL_TEXT Base = Component;
  OABL_TEXT Ext  = TextSub (Component, Component.Len, Component.Len);
  UINTN     Dot;

  for (INT32 Index = 0; Index < 11; Index++) {
    Name[Index] = ' ';
  }
  if (TextFindLastChar (Component, '.', &Dot)) {
    Base = TextSub (Component, 0, Dot);
    Ext  = TextSub (Component, Dot + 1, Component.Len);
  }
  for (UINTN Index = 0; Index < 8 && Index < Base.Len; Index++) {
    Name[Index] = Upper (Base.Ptr[Index]);
  }
  for (UINTN Index = 0; Index < 3 && Index < Ext.Len; Index++) {
    Name[8 + Index] = Upper (Ext.Ptr[Index]);
  }
}

BOOLEAN
Fat32Mount (
  OUT FAT32_VOLUME *V,
  IN UINT32         BlockSize,
  IN FAT32_READ    *Read,
  IN VOID          *Context
)
{
  UINT8  Bpb[512];
  UINT32 BytesPerSector;
  UINT32 SectorsPerCluster;
  UINT32 Reserved;
  UINT32 Fats;
  UINT32 FatSize16;
  UINT32 Total;
  UINT32 FatSize;
  UINT32 Root;
  UINT64 DataStart;

  if (!Read (0, Bpb, sizeof (Bpb), Context)) {
    return FALSE;
  }
  if ((Bpb[0] != 0xEB && Bpb[0] != 0xE9) || Bpb[510] != 0x55 ||
      Bpb[511] != 0xAA) {
    return FALSE;
  }
  BytesPerSector    = ReadLe16 (Bpb + 11);
  SectorsPerCluster = Bpb[13];
  Reserved          = ReadLe16 (Bpb + 14);
  Fats              = Bpb[16];
  FatSize16         = ReadLe16 (Bpb + 22);
  Total             = ReadLe32 (Bpb + 32);
  FatSize           = ReadLe32 (Bpb + 36);
  Root              = ReadLe32 (Bpb + 44);
  // FAT12 and FAT16 set the 16-bit FAT size. FAT32 leaves it zero.
  if (BytesPerSector != BlockSize || BytesPerSector == 0 ||
      SectorsPerCluster == 0 || Fats == 0 || FatSize16 != 0 || FatSize == 0 ||
      Reserved == 0) {
    return FALSE;
  }
  DataStart = Reserved + (UINT64)Fats * FatSize;
  if (Total <= DataStart) {
    return FALSE;
  }
  *V = (FAT32_VOLUME){
    .Read              = Read,
    .Ctx               = Context,
    .BlockSize         = BlockSize,
    .SectorsPerCluster = SectorsPerCluster,
    .ClusterBytes      = BytesPerSector * SectorsPerCluster,
    .FatStart          = Reserved,
    .DataStart         = DataStart,
    .Root              = Root,
    .MaxCluster        = (UINT32)((Total - DataStart) / SectorsPerCluster) + 1,
  };
  CopyMem (V->Label, Bpb + 71, 11);
  return TRUE;
}

VOID
Fat32Free (IN OUT FAT32_VOLUME *V)
{
  FreePool (V->FatCache);
  V->FatCache  = NULL;
  V->FatCached = FALSE;
}

STATIC BOOLEAN
Valid (
  IN CONST FAT32_VOLUME *V,
  IN UINT32              Cluster
)
{
  return Cluster >= 2 && Cluster <= V->MaxCluster && Cluster < CHAIN_END;
}

STATIC UINT64
ClusterOffset (
  IN CONST FAT32_VOLUME *V,
  IN UINT32              Cluster
)
{
  return (V->DataStart + (UINT64)(Cluster - 2) * V->SectorsPerCluster) *
         V->BlockSize;
}

// Reads the 28-bit FAT entry for Cluster. One FAT sector is cached.
STATIC BOOLEAN
Next (
  IN OUT FAT32_VOLUME *V,
  IN UINT32            Cluster,
  OUT UINT32          *NextCluster
)
{
  UINT64 BlockSize = V->BlockSize;
  UINT64 Offset    = V->FatStart * BlockSize + (UINT64)Cluster * 4;
  UINT64 Sector    = Offset / BlockSize;
  UINTN  At        = (UINTN)(Offset - Sector * BlockSize);

  if (!V->FatCached || V->FatSector != Sector) {
    V->FatCached = FALSE;
    if (!V->FatCache) {
      V->FatCache = AllocateZeroPool ((UINTN)BlockSize);
    }
    if (!V->FatCache ||
        !V->Read (Sector * BlockSize, V->FatCache, (UINTN)BlockSize, V->Ctx)) {
      return FALSE;
    }
    V->FatCached = TRUE;
    V->FatSector = Sector;
  }
  // Only possible with a sector size that isn't a multiple of 4.
  if (At + 4 > BlockSize) {
    return FALSE;
  }
  *NextCluster = ReadLe32 (V->FatCache + At) & 0x0FFFFFFF;
  return TRUE;
}

STATIC BOOLEAN
FindIn (
  IN OUT FAT32_VOLUME *V,
  IN UINT32            Cluster,
  IN CONST UINT8       Name[11],
  OUT UINT32          *First,
  OUT UINT32          *Size,
  OUT BOOLEAN         *IsDir
)
{
  UINT8  *Buffer = AllocateZeroPool (V->ClusterBytes);
  BOOLEAN Found  = FALSE;

  if (!Buffer) {
    return FALSE;
  }
  // Bound the walk so that a loop in the FAT can't hang it.
  for (UINT64 Visited = 0; Visited <= V->MaxCluster; Visited++) {
    if (!Valid (V, Cluster)) {
      break;
    }
    if (!V->Read (
          ClusterOffset (V, Cluster), Buffer, V->ClusterBytes, V->Ctx
        )) {
      break;
    }
    for (UINTN At = 0; At + 32 <= V->ClusterBytes; At += 32) {
      CONST UINT8 *Entry = Buffer + At;
      BOOLEAN      Same  = TRUE;

      if (Entry[0] == 0) {
        goto Done;
      }
      if (Entry[0] == 0xE5 || Entry[11] == ATTR_LFN ||
          Entry[11] & ATTR_VOLUME) {
        continue;
      }
      for (INT32 Index = 0; Index < 11; Index++) {
        Same = Same && Entry[Index] == Name[Index];
      }
      if (Same) {
        *First = (UINT32)ReadLe16 (Entry + 20) << 16 | ReadLe16 (Entry + 26);
        *Size  = ReadLe32 (Entry + 28);
        *IsDir = (Entry[11] & ATTR_DIR) != 0;
        Found  = TRUE;
        goto Done;
      }
    }
    if (!Next (V, Cluster, &Cluster)) {
      break;
    }
  }
Done:
  FreePool (Buffer);
  return Found;
}

BOOLEAN
Fat32Find (
  IN OUT FAT32_VOLUME *V,
  IN CONST CHAR8      *Path,
  OUT FAT32_FILE      *File
)
{
  OABL_TEXT Rest = TextOf (Path);
  OABL_TEXT Part;
  OABL_TEXT Last = {0};
  BOOLEAN   Done = FALSE;
  BOOLEAN   Have = FALSE;
  BOOLEAN   IsDir;
  UINT32    Dir = V->Root;
  UINT32    Cluster;
  UINT32    Size;
  UINT8     Name[11];

  // Every component but the last must be a directory.
  while (TextNextSplit (&Rest, '\\', &Done, &Part)) {
    if (!Part.Len) {
      continue;
    }
    if (Have) {
      ShortName (Last, Name);
      if (!FindIn (V, Dir, Name, &Cluster, &Size, &IsDir) || !IsDir) {
        return FALSE;
      }
      Dir = Cluster;
    }
    Last = Part;
    Have = TRUE;
  }
  if (!Have) {
    return FALSE;
  }
  ShortName (Last, Name);
  if (!FindIn (V, Dir, Name, &Cluster, &Size, &IsDir) || IsDir) {
    return FALSE;
  }
  *File = (FAT32_FILE){Cluster, Size};
  return TRUE;
}

BOOLEAN
Fat32Read (
  IN OUT FAT32_VOLUME *V,
  IN CONST FAT32_FILE *File,
  IN UINT64            Offset,
  IN UINTN             Length,
  OUT OABL_BUFFER     *Out
)
{
  UINT64 ClusterBytes = V->ClusterBytes;
  UINT64 Left;
  UINT32 Cluster = File->Cluster;
  UINT32 Limit;
  UINT32 Steps = 0;
  UINTN  Want;
  UINTN  Skip;
  UINTN  Done = 0;
  UINT8 *Buffer;

  if (Offset >= File->Size) {
    return FALSE;
  }
  Left = File->Size - Offset;
  Want = (UINTN)(Left < Length ? Left : Length);
  // Count every cluster visited so that a loop in the FAT fails the read.
  Limit = V->MaxCluster + 8;
  for (UINT64 Index = 0; Index < Offset / ClusterBytes; Index++) {
    Steps++;
    if (!Valid (V, Cluster) || Steps > Limit) {
      return FALSE;
    }
    if (!Next (V, Cluster, &Cluster)) {
      return FALSE;
    }
  }
  Skip   = (UINTN)(Offset % ClusterBytes);
  Buffer = AllocateZeroPool (Want ? Want : 1);
  if (!Buffer) {
    return FALSE;
  }
  while (Done < Want) {
    UINT32  Run   = 1;
    UINT32  Last  = Cluster;
    UINT32  After = 0;
    UINT32  NextCluster;
    BOOLEAN HaveAfter = FALSE;
    UINTN   Chunk;

    if (!Valid (V, Cluster)) {
      goto Fail;
    }
    // Read a run of consecutive clusters in one call.
    while ((UINT64)Run * ClusterBytes - Skip < (UINT64)(Want - Done)) {
      if (!Next (V, Last, &NextCluster)) {
        goto Fail;
      }
      if (NextCluster != Last + 1 || !Valid (V, NextCluster)) {
        After     = NextCluster;
        HaveAfter = TRUE;
        break;
      }
      Last = NextCluster;
      Run++;
      Steps++;
      if (Steps > Limit) {
        goto Fail;
      }
    }
    Chunk = (UINTN)((UINT64)Run * ClusterBytes) - Skip;
    if (Chunk > Want - Done) {
      Chunk = Want - Done;
    }
    if (!V->Read (
          ClusterOffset (V, Cluster) + Skip, Buffer + Done, Chunk, V->Ctx
        )) {
      goto Fail;
    }
    Done += Chunk;
    Skip = 0;
    if (Done >= Want) {
      break;
    }
    if (HaveAfter) {
      Cluster = After;
    } else if (!Next (V, Last, &Cluster)) {
      goto Fail;
    }
    Steps++;
    if (Steps > Limit) {
      goto Fail;
    }
  }
  *Out = (OABL_BUFFER){Buffer, Want, Want ? Want : 1};
  return TRUE;

Fail:
  FreePool (Buffer);
  return FALSE;
}
