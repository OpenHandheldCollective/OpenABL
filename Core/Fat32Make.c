/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Fat32Make - writes a new FAT32 volume holding a given set of files.
*/

#include "Core/Fat32Make.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"

// As mkfs.fat: 32 reserved sectors, two FATs, the backup boot sector at 6.
#define RESERVED     32
#define FATS         2
#define FS_INFO      1
#define BACKUP_BOOT  6
#define CLUSTER      4096u
#define MIN_CLUSTERS 65525u
#define MAX_CLUSTERS 0x0FFFFFF5u
#define CHAIN_END    0x0FFFFFFFu
#define ENTRY        32
#define LFN_CHARS    13
#define MAX_NAME     255
#define ATTR_LABEL   0x08
#define ATTR_DIR     0x10
#define ATTR_FILE    0x20
#define ATTR_LFN     0x0F
// FAT chunk written at a time.
#define CHUNK 65536u

STATIC CHAR16
UpperChar (IN CHAR16 C)
{
  return C >= 'a' && C <= 'z' ? (CHAR16)(C - 32) : C;
}

// Characters a short name may hold besides A-Z and 0-9.
STATIC BOOLEAN
ShortChar (IN CHAR16 C)
{
  STATIC CONST CHAR8 Extra[] = "!#$%&'()-@^_`{}~";

  if ((C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9')) {
    return TRUE;
  }
  for (UINTN Index = 0; Extra[Index]; Index++) {
    if (C == (CHAR16)Extra[Index]) {
      return TRUE;
    }
  }
  return FALSE;
}

STATIC UINTN
NameLen (IN CONST CHAR16 *Name)
{
  UINTN Len = 0;

  while (Name[Len]) {
    Len++;
  }
  return Len;
}

// Whether two names match, ignoring ASCII case.
STATIC BOOLEAN
SameName (
  IN CONST CHAR16 *A,
  IN CONST CHAR16 *B
)
{
  while (*A && UpperChar (*A) == UpperChar (*B)) {
    A++;
    B++;
  }
  return *A == *B;
}

// The name as an exact 8.3 short name, if it is one: upper case, a base
// of 1 to 8 characters and an extension of up to 3.
STATIC BOOLEAN
ExactShort (
  IN CONST CHAR16 *Name,
  OUT UINT8        Short[11]
)
{
  UINTN Len  = NameLen (Name);
  UINTN Dot  = Len;
  UINTN Base;

  for (UINTN Index = 0; Index < Len; Index++) {
    if (Name[Index] == '.') {
      if (Dot != Len) {
        return FALSE;
      }
      Dot = Index;
    } else if (!ShortChar (Name[Index])) {
      return FALSE;
    }
  }
  Base = Dot;
  if (Base == 0 || Base > 8 || (Dot < Len && (Len - Dot - 1 == 0 ||
                                              Len - Dot - 1 > 3))) {
    return FALSE;
  }
  SetMem (Short, 11, ' ');
  for (UINTN Index = 0; Index < Base; Index++) {
    Short[Index] = (UINT8)Name[Index];
  }
  for (UINTN Index = Dot + 1; Index < Len; Index++) {
    Short[8 + Index - Dot - 1] = (UINT8)Name[Index];
  }
  return TRUE;
}

// A short name for a long one: up to 6 characters of its base, ~Tail, and
// up to 3 of its last extension, upper case, others replaced by '_'.
STATIC VOID
GeneratedShort (
  IN CONST CHAR16 *Name,
  IN UINT32        Tail,
  OUT UINT8        Short[11]
)
{
  UINTN Len = NameLen (Name);
  UINTN Dot = Len;
  UINTN At  = 0;
  CHAR8 Digits[8];
  UINTN DigitCount = 0;

  for (UINTN Index = Len; Index > 1; Index--) {
    if (Name[Index - 1] == '.') {
      Dot = Index - 1;
      break;
    }
  }
  do {
    Digits[DigitCount++] = (CHAR8)('0' + Tail % 10);
    Tail /= 10;
  } while (Tail && DigitCount < sizeof (Digits));
  SetMem (Short, 11, ' ');
  for (UINTN Index = 0; Index < Dot && At < 7 - DigitCount; Index++) {
    CHAR16 C = UpperChar (Name[Index]);
    if (C == ' ' || C == '.') {
      continue;
    }
    Short[At++] = ShortChar (C) ? (UINT8)C : '_';
  }
  Short[At++] = '~';
  while (DigitCount) {
    Short[At++] = (UINT8)Digits[--DigitCount];
  }
  At = 8;
  for (UINTN Index = Dot + 1; Index < Len && At < 11; Index++) {
    CHAR16 C = UpperChar (Name[Index]);
    if (C == ' ') {
      continue;
    }
    Short[At++] = ShortChar (C) ? (UINT8)C : '_';
  }
}

// Whether a node in Parent other than Self has this short name. Nodes
// after Self count only once they have one, which Fat32MakePlan gives
// exact names first.
STATIC BOOLEAN
ShortTaken (
  IN CONST FAT32_PLAN *Plan,
  IN INTN              Parent,
  IN UINTN             Self,
  IN CONST UINT8       Short[11],
  IN CONST BOOLEAN    *Named
)
{
  for (UINTN Index = 0; Index < Plan->Count; Index++) {
    if (Index != Self && Named[Index] && Plan->Nodes[Index].Parent == Parent &&
        CompareMem (Plan->Nodes[Index].Short, Short, 11) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

STATIC UINT32
LfnEntries (IN CONST FAT32_NODE *Node)
{
  return Node->Long ? (UINT32)((NameLen (Node->Name) + LFN_CHARS - 1) /
                               LFN_CHARS)
                    : 0;
}

// Entries in a directory: its nodes' long-name and short entries, plus
// the label in the root or . and .. elsewhere.
STATIC UINT64
DirEntries (
  IN CONST FAT32_PLAN *Plan,
  IN INTN              Dir
)
{
  UINT64 Count = Dir < 0 ? 1 : 2;

  for (UINTN Index = 0; Index < Plan->Count; Index++) {
    if (Plan->Nodes[Index].Parent == Dir) {
      Count += 1 + LfnEntries (&Plan->Nodes[Index]);
    }
  }
  return Count;
}

STATIC UINT64
ClustersFor (IN UINT64 Bytes)
{
  return (Bytes + CLUSTER - 1) / CLUSTER;
}

STATIC BOOLEAN
NameNodes (IN OUT FAT32_PLAN *Plan)
{
  BOOLEAN *Named = AllocateZeroArray (Plan->Count ? Plan->Count : 1, 1);
  BOOLEAN  Ok    = FALSE;

  if (!Named) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Plan->Count; Index++) {
    FAT32_NODE *Node = &Plan->Nodes[Index];
    UINTN       Len  = NameLen (Node->Name);

    if (Len == 0 || Len > MAX_NAME || Node->Parent >= (INTN)Index ||
        (Node->Parent >= 0 && !Plan->Nodes[Node->Parent].Dir) ||
        (Node->Name[0] == '.' && (Len == 1 || (Len == 2 &&
                                               Node->Name[1] == '.')))) {
      goto Done;
    }
    for (UINTN Other = 0; Other < Index; Other++) {
      if (Plan->Nodes[Other].Parent == Node->Parent &&
          SameName (Plan->Nodes[Other].Name, Node->Name)) {
        goto Done;
      }
    }
    // Exact names first, so a generated one can't take theirs.
    if (ExactShort (Node->Name, Node->Short)) {
      Node->Long   = FALSE;
      Named[Index] = TRUE;
    }
  }
  for (UINTN Index = 0; Index < Plan->Count; Index++) {
    FAT32_NODE *Node = &Plan->Nodes[Index];
    UINT32      Tail = 1;

    if (Named[Index]) {
      if (ShortTaken (Plan, Node->Parent, Index, Node->Short, Named)) {
        goto Done;
      }
      continue;
    }
    Node->Long = TRUE;
    do {
      GeneratedShort (Node->Name, Tail++, Node->Short);
    } while (ShortTaken (Plan, Node->Parent, Index, Node->Short, Named) &&
             Tail < 1000000);
    if (Tail >= 1000000) {
      goto Done;
    }
    Named[Index] = TRUE;
  }
  Ok = TRUE;
Done:
  FreePool (Named);
  return Ok;
}

BOOLEAN
Fat32MakePlan (IN OUT FAT32_PLAN *Plan)
{
  UINT64 Sectors;
  UINT64 Fat = 1;
  UINT64 Clusters;
  UINT64 Next;

  if (Plan->SectorSize < 512 || Plan->SectorSize > CLUSTER ||
      (Plan->SectorSize & (Plan->SectorSize - 1)) || !NameNodes (Plan)) {
    return FALSE;
  }
  Sectors = Plan->Bytes / Plan->SectorSize;
  if (Sectors > MAX_UINT32) {
    return FALSE;
  }
  // Grow the FATs until they cover the clusters left after them.
  for (;;) {
    UINT64 Need;
    if (Sectors <= RESERVED + FATS * Fat) {
      return FALSE;
    }
    Clusters = (Sectors - RESERVED - FATS * Fat) /
               (CLUSTER / Plan->SectorSize);
    Need = ((Clusters + 2) * 4 + Plan->SectorSize - 1) / Plan->SectorSize;
    if (Need <= Fat) {
      break;
    }
    Fat = Need;
  }
  if (Clusters < MIN_CLUSTERS || Clusters > MAX_CLUSTERS) {
    return FALSE;
  }
  Plan->Reserved     = RESERVED;
  Plan->FatSectors   = (UINT32)Fat;
  Plan->ClusterSize  = CLUSTER;
  Plan->Clusters     = (UINT32)Clusters;
  Plan->RootClusters = (UINT32)ClustersFor (DirEntries (Plan, -1) * ENTRY);
  Next               = 2 + Plan->RootClusters;
  for (UINTN Index = 0; Index < Plan->Count; Index++) {
    FAT32_NODE *Node = &Plan->Nodes[Index];
    UINT64      Count;

    if (Node->Dir) {
      Count = ClustersFor (DirEntries (Plan, (INTN)Index) * ENTRY);
    } else if (Node->Size > MAX_UINT32) {
      return FALSE;
    } else {
      Count = ClustersFor (Node->Size);
    }
    Node->Cluster  = Count ? (UINT32)Next : 0;
    Node->Clusters = (UINT32)Count;
    Next          += Count;
    if (Next - 2 > Clusters) {
      return FALSE;
    }
  }
  Plan->Used = (UINT32)(Next - 2);
  return TRUE;
}

UINT64
Fat32MakeOffset (
  IN CONST FAT32_PLAN *Plan,
  IN UINT32            Cluster
)
{
  UINT64 Data = (UINT64)(Plan->Reserved + FATS * Plan->FatSectors) *
                Plan->SectorSize;

  return Data + (UINT64)(Cluster - 2) * Plan->ClusterSize;
}

// Fills a short entry.
STATIC VOID
PutEntry (
  OUT UINT8       *Entry,
  IN CONST UINT8   Name[11],
  IN UINT8         Attr,
  IN UINT32        Cluster,
  IN UINT32        Size,
  IN UINT16        Date,
  IN UINT16        Time
)
{
  ZeroMem (Entry, ENTRY);
  CopyMem (Entry, Name, 11);
  Entry[11] = Attr;
  WriteLe16 (Entry + 14, Time);
  WriteLe16 (Entry + 16, Date);
  WriteLe16 (Entry + 18, Date);
  WriteLe16 (Entry + 20, (UINT16)(Cluster >> 16));
  WriteLe16 (Entry + 22, Time);
  WriteLe16 (Entry + 24, Date);
  WriteLe16 (Entry + 26, (UINT16)Cluster);
  WriteLe32 (Entry + 28, Size);
}

STATIC UINT8
ShortSum (IN CONST UINT8 Short[11])
{
  UINT8 Sum = 0;

  for (INT32 Index = 0; Index < 11; Index++) {
    Sum = (UINT8)(((Sum & 1) << 7) + (Sum >> 1) + Short[Index]);
  }
  return Sum;
}

// Writes a node's long-name entries, last part first, then its short entry.
// Returns the entries written.
STATIC UINT32
PutNode (
  OUT UINT8            *At,
  IN CONST FAT32_NODE  *Node
)
{
  // Unit offsets of the 13 characters in a long-name entry.
  STATIC CONST UINT8 Slots[LFN_CHARS] = {1,  3,  5,  7,  9,  14, 16,
                                         18, 20, 22, 24, 28, 30};
  UINT32 Parts = LfnEntries (Node);
  UINTN  Len   = NameLen (Node->Name);
  UINT8  Sum   = ShortSum (Node->Short);

  for (UINT32 Part = Parts; Part > 0; Part--) {
    UINT8 *Entry = At + (Parts - Part) * ENTRY;
    ZeroMem (Entry, ENTRY);
    Entry[0]  = (UINT8)(Part | (Part == Parts ? 0x40 : 0));
    Entry[11] = ATTR_LFN;
    Entry[13] = Sum;
    for (UINT32 Index = 0; Index < LFN_CHARS; Index++) {
      UINTN  Char = (Part - 1) * LFN_CHARS + Index;
      // The name ends with a NUL if there's room, then 0xFFFF padding.
      UINT16 Unit = Char < Len ? Node->Name[Char] : Char == Len ? 0 : 0xFFFF;
      WriteLe16 (Entry + Slots[Index], Unit);
    }
  }
  PutEntry (
    At + Parts * ENTRY, Node->Short, Node->Dir ? ATTR_DIR : ATTR_FILE,
    Node->Cluster, Node->Dir ? 0 : (UINT32)Node->Size, Node->Date, Node->Time
  );
  return Parts + 1;
}

// Writes directory Dir (-1 for the root) to its clusters.
STATIC BOOLEAN
WriteDir (
  IN CONST FAT32_PLAN *Plan,
  IN INTN              Dir,
  IN FAT32_WRITE      *Write,
  IN VOID             *Context
)
{
  STATIC CONST UINT8 Dot[12]    = ".          ";
  STATIC CONST UINT8 DotDot[12] = "..         ";
  UINT32             Cluster;
  UINT32             Clusters;
  UINT8             *Data;
  UINT8             *At;
  BOOLEAN            Ok;

  if (Dir < 0) {
    Cluster  = 2;
    Clusters = Plan->RootClusters;
  } else {
    Cluster  = Plan->Nodes[Dir].Cluster;
    Clusters = Plan->Nodes[Dir].Clusters;
  }
  Data = AllocateZeroArray (Clusters, Plan->ClusterSize);
  if (!Data) {
    return FALSE;
  }
  At = Data;
  if (Dir < 0) {
    UINT8 Label[11];
    SetMem (Label, 11, ' ');
    for (UINTN Index = 0; Index < 11 && Plan->Label[Index]; Index++) {
      Label[Index] = (UINT8)Plan->Label[Index];
    }
    PutEntry (At, Label, ATTR_LABEL, 0, 0, Plan->Date, Plan->Time);
    At += ENTRY;
  } else {
    CONST FAT32_NODE *Self   = &Plan->Nodes[Dir];
    INTN              Parent = Self->Parent;
    PutEntry (At, Dot, ATTR_DIR, Self->Cluster, 0, Self->Date, Self->Time);
    // .. points at cluster 0 when the parent is the root.
    PutEntry (
      At + ENTRY, DotDot, ATTR_DIR,
      Parent < 0 ? 0 : Plan->Nodes[Parent].Cluster, 0, Self->Date,
      Self->Time
    );
    At += 2 * ENTRY;
  }
  for (UINTN Index = 0; Index < Plan->Count; Index++) {
    if (Plan->Nodes[Index].Parent == Dir) {
      At += PutNode (At, &Plan->Nodes[Index]) * ENTRY;
    }
  }
  Ok = Write (
    Fat32MakeOffset (Plan, Cluster), Data,
    (UINTN)Clusters * Plan->ClusterSize, Context
  );
  FreePool (Data);
  return Ok;
}

// Returns the last cluster of the next node's run after node *Run, and
// moves *Run past it. Runs follow node order, after the root's.
STATIC UINT32
NextRunEnd (
  IN CONST FAT32_PLAN *Plan,
  IN OUT UINTN        *Run
)
{
  while (*Run < Plan->Count) {
    CONST FAT32_NODE *Node = &Plan->Nodes[(*Run)++];
    if (Node->Clusters) {
      return Node->Cluster + Node->Clusters - 1;
    }
  }
  return 0;
}

STATIC BOOLEAN
WriteFats (
  IN CONST FAT32_PLAN *Plan,
  IN FAT32_WRITE      *Write,
  IN VOID             *Context
)
{
  UINT64  Bytes = (UINT64)Plan->FatSectors * Plan->SectorSize;
  UINT8  *Chunk = AllocatePool (CHUNK);
  BOOLEAN Ok    = TRUE;
  UINTN   Run   = 0;
  // The root's run comes first.
  UINT32  End   = 1 + Plan->RootClusters;

  if (!Chunk) {
    return FALSE;
  }
  for (UINT64 At = 0; Ok && At < Bytes; At += CHUNK) {
    UINTN Size = Bytes - At < CHUNK ? (UINTN)(Bytes - At) : CHUNK;
    for (UINTN Offset = 0; Offset < Size; Offset += 4) {
      UINT64 Cluster = (At + Offset) / 4;
      UINT32 Value   = 0;
      if (Cluster == 0) {
        Value = 0x0FFFFF00u | 0xF8;
      } else if (Cluster == 1) {
        Value = CHAIN_END;
      } else if (Cluster == End) {
        Value = CHAIN_END;
        End   = NextRunEnd (Plan, &Run);
      } else if (Cluster < 2 + (UINT64)Plan->Used) {
        Value = (UINT32)Cluster + 1;
      }
      WriteLe32 (Chunk + Offset, Value);
    }
    for (UINT32 Copy = 0; Ok && Copy < FATS; Copy++) {
      UINT64 Fat = (UINT64)(Plan->Reserved + Copy * Plan->FatSectors) *
                   Plan->SectorSize;
      Ok = Write (Fat + At, Chunk, Size, Context);
    }
  }
  FreePool (Chunk);
  return Ok;
}

// The reserved sectors: boot sector and FS information sector, with
// their backups at 6 and 7.
STATIC BOOLEAN
WriteBoot (
  IN CONST FAT32_PLAN *Plan,
  IN FAT32_WRITE      *Write,
  IN VOID             *Context
)
{
  STATIC CONST UINT8 Jump[3] = {0xEB, 0x58, 0x90};
  UINTN              Size    = (UINTN)Plan->Reserved * Plan->SectorSize;
  UINT8             *Data    = AllocateZeroPool (Size);
  UINT8             *Boot;
  UINT8             *Info;
  BOOLEAN            Ok;

  if (!Data) {
    return FALSE;
  }
  Boot = Data;
  CopyMem (Boot, Jump, 3);
  CopyMem (Boot + 3, "mkfs.fat", 8);
  WriteLe16 (Boot + 11, (UINT16)Plan->SectorSize);
  Boot[13] = (UINT8)(Plan->ClusterSize / Plan->SectorSize);
  WriteLe16 (Boot + 14, (UINT16)Plan->Reserved);
  Boot[16] = FATS;
  Boot[21] = 0xF8;
  WriteLe16 (Boot + 24, 63);
  WriteLe16 (Boot + 26, 255);
  WriteLe32 (Boot + 28, Plan->HiddenSectors);
  WriteLe32 (Boot + 32, (UINT32)(Plan->Bytes / Plan->SectorSize));
  WriteLe32 (Boot + 36, Plan->FatSectors);
  WriteLe32 (Boot + 44, 2);
  WriteLe16 (Boot + 48, FS_INFO);
  WriteLe16 (Boot + 50, BACKUP_BOOT);
  Boot[64] = 0x80;
  Boot[66] = 0x29;
  WriteLe32 (Boot + 67, Plan->Serial);
  SetMem (Boot + 71, 11, ' ');
  for (UINTN Index = 0; Index < 11 && Plan->Label[Index]; Index++) {
    Boot[71 + Index] = (UINT8)Plan->Label[Index];
  }
  CopyMem (Boot + 82, "FAT32   ", 8);
  Boot[510] = 0x55;
  Boot[511] = 0xAA;
  Info = Data + FS_INFO * Plan->SectorSize;
  WriteLe32 (Info, 0x41615252);
  WriteLe32 (Info + 484, 0x61417272);
  WriteLe32 (Info + 488, Plan->Clusters - Plan->Used);
  WriteLe32 (Info + 492, 2 + Plan->Used);
  WriteLe32 (Info + 508, 0xAA550000);
  CopyMem (Data + BACKUP_BOOT * Plan->SectorSize, Boot, 2 * Plan->SectorSize);
  // The boot sector goes last, after its backup.
  Ok = Write (
         Plan->SectorSize, Data + Plan->SectorSize, Size - Plan->SectorSize,
         Context
       ) &&
       Write (0, Data, Plan->SectorSize, Context);
  FreePool (Data);
  return Ok;
}

BOOLEAN
Fat32MakeWrite (
  IN CONST FAT32_PLAN *Plan,
  IN FAT32_WRITE      *Write,
  IN VOID             *Context
)
{
  if (!WriteFats (Plan, Write, Context) || !WriteDir (Plan, -1, Write, Context)) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Plan->Count; Index++) {
    if (Plan->Nodes[Index].Dir &&
        !WriteDir (Plan, (INTN)Index, Write, Context)) {
      return FALSE;
    }
  }
  return WriteBoot (Plan, Write, Context);
}
