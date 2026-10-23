/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Ext4Make - writes a new, empty ext4 file system.

  Layout, as mke2fs's: 32768 blocks per group, sixteen groups per flex
  group with their bitmaps and inode tables at the start of the first, the
  superblock and descriptors backed up in groups 1 and the powers of 3, 5
  and 7. Groups with nothing but what the kernel works out on its own are
  left uninitialised.
*/

#include "Core/Ext4Make.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Base/Text.h"

#define BLOCK       4096u
#define PER_GROUP   32768u
#define INODE       256u
#define PER_BLOCK   (BLOCK / INODE)
#define DESC        64u
#define FLEX        16u
#define LOG_FLEX    4
#define FIRST_INO   11u
#define ROOT_INO    2u
#define JOURNAL_INO 8u
#define LOST_INO    11u
#define LOST_BLOCKS 4u
#define EXTRA_ISIZE 32u
#define MAX_EXTENT  32768u
#define MAX_EXTENTS 4u
#define MIN_BYTES   (64ull << 20)
// Keeps every count in the superblock's low fields.
#define MAX_BLOCKS 0xFFFFFFFFull
// Directory block tail: inode 0, length 12, type 0xDE, checksum.
#define TAIL 12u

// has_journal, ext_attr, dir_index
#define COMPAT 0x002Cu
// filetype, extent, 64bit, flex_bg, metadata_csum_seed
#define INCOMPAT 0x22C2u
// sparse_super, large_file, huge_file, dir_nlink, extra_isize, metadata_csum
#define RO_COMPAT 0x046Bu

#define BG_INODE_UNINIT 0x1
#define BG_BLOCK_UNINIT 0x2

#define MODE_DIR  0x4000u
#define MODE_FILE 0x8000u
#define EXTENTS_FL 0x80000u

typedef struct {
  UINT64 Start;
  UINT32 Count;
} EXTENT;

typedef struct {
  CONST EXT4_MAKE *Params;
  EXT4_WRITE      *Write;
  VOID            *Context;
  UINT64           Blocks;
  UINT32           Groups;
  UINT32           Ipg;       // inodes per group
  UINT32           Itb;       // inode table blocks per group
  UINT32           GdtBlocks; // group descriptor blocks
  UINT32           Seed;      // checksum seed, from the UUID
  // Every group's block bitmap, end to end; bits past the end are set.
  UINT8  *Bitmap;
  UINT64 *BlockBitmap;
  UINT64 *InodeBitmap;
  UINT64 *InodeTable;
  UINT64  Root;
  UINT64  Lost;
  EXTENT  Journal[MAX_EXTENTS];
  UINT32  JournalExtents;
  UINT8  *Gdt;
  UINT64  FreeBlocks;
} MAKE;

// CRC-32C without the usual inversions, as ext4 chains it.
STATIC UINT32
Crc32c (
  IN UINT32      Crc,
  IN CONST VOID *Buffer,
  IN UINTN       Length
)
{
  CONST UINT8 *Bytes = Buffer;

  for (UINTN Index = 0; Index < Length; Index++) {
    Crc ^= Bytes[Index];
    for (INT32 Bit = 0; Bit < 8; Bit++) {
      Crc = Crc & 1 ? (Crc >> 1) ^ 0x82F63B78u : Crc >> 1;
    }
  }
  return Crc;
}

STATIC UINT32
Crc32cLe32 (
  IN UINT32 Crc,
  IN UINT32 Value
)
{
  UINT8 Bytes[4];

  WriteLe32 (Bytes, Value);
  return Crc32c (Crc, Bytes, 4);
}

STATIC BOOLEAN
PowerOf (
  IN UINT32 Value,
  IN UINT32 Base
)
{
  while (Value % Base == 0) {
    Value /= Base;
  }
  return Value == 1;
}

// sparse_super: groups 0, 1 and the powers of 3, 5 and 7 hold a backup.
STATIC BOOLEAN
HasSuper (IN UINT32 Group)
{
  return Group <= 1 || PowerOf (Group, 3) || PowerOf (Group, 5) ||
         PowerOf (Group, 7);
}

STATIC UINT64
GroupStart (IN UINT32 Group)
{
  return (UINT64)Group * PER_GROUP;
}

STATIC UINT32
GroupLen (
  IN CONST MAKE *M,
  IN UINT32      Group
)
{
  return Group + 1 == M->Groups ? (UINT32)(M->Blocks - GroupStart (Group))
                                : PER_GROUP;
}

// The superblock and descriptor blocks at the start of a group.
STATIC UINT32
BaseMeta (
  IN CONST MAKE *M,
  IN UINT32      Group
)
{
  return HasSuper (Group) ? 1 + M->GdtBlocks : 0;
}

STATIC BOOLEAN
Used (
  IN CONST MAKE *M,
  IN UINT64      Block
)
{
  return (M->Bitmap[Block / 8] >> (Block % 8)) & 1;
}

STATIC VOID
Mark (
  IN OUT MAKE *M,
  IN UINT64    Block
)
{
  M->Bitmap[Block / 8] |= (UINT8)(1 << (Block % 8));
}

// Takes Count free blocks from First. FALSE if one is taken or past the
// end.
STATIC BOOLEAN
Claim (
  IN OUT MAKE *M,
  IN UINT64    First,
  IN UINT64    Count
)
{
  if (First + Count > M->Blocks) {
    return FALSE;
  }
  for (UINT64 Block = First; Block < First + Count; Block++) {
    if (Used (M, Block)) {
      return FALSE;
    }
  }
  for (UINT64 Block = First; Block < First + Count; Block++) {
    Mark (M, Block);
  }
  return TRUE;
}

// Takes the first run of Count free blocks.
STATIC BOOLEAN
Allocate (
  IN OUT MAKE *M,
  IN UINT64    Count,
  OUT UINT64  *First
)
{
  UINT64 Run = 0;

  for (UINT64 Block = 0; Block < M->Blocks; Block++) {
    Run = Used (M, Block) ? 0 : Run + 1;
    if (Run == Count) {
      *First = Block + 1 - Count;
      return Claim (M, *First, Count);
    }
  }
  return FALSE;
}

// Places the groups' metadata and the root, lost+found and journal.
STATIC BOOLEAN
Layout (IN OUT MAKE *M)
{
  UINT64 Journal = M->Params->JournalBlocks;

  // Bits past the last block stay set.
  for (UINT64 Block = M->Blocks; Block < GroupStart (M->Groups); Block++) {
    Mark (M, Block);
  }
  for (UINT32 Group = 0; Group < M->Groups; Group++) {
    if (!Claim (M, GroupStart (Group), BaseMeta (M, Group))) {
      return FALSE;
    }
  }
  for (UINT32 Leader = 0; Leader < M->Groups; Leader += FLEX) {
    UINT32 Count = M->Groups - Leader < FLEX ? M->Groups - Leader : FLEX;
    UINT64 At    = GroupStart (Leader) + BaseMeta (M, Leader);
    for (UINT32 Index = 0; Index < Count; Index++) {
      M->BlockBitmap[Leader + Index] = At + Index;
      M->InodeBitmap[Leader + Index] = At + Count + Index;
      M->InodeTable[Leader + Index]  = At + 2 * Count +
                                       (UINT64)Index * M->Itb;
    }
    if (!Claim (M, At, 2 * Count + (UINT64)Count * M->Itb)) {
      return FALSE;
    }
  }
  if (!Allocate (M, 1, &M->Root) || !Allocate (M, LOST_BLOCKS, &M->Lost)) {
    return FALSE;
  }
  // The journal in runs of up to an extent each.
  while (Journal) {
    UINT32 Count = Journal < MAX_EXTENT ? (UINT32)Journal : MAX_EXTENT;
    EXTENT *Extent;
    if (M->JournalExtents == MAX_EXTENTS) {
      return FALSE;
    }
    Extent = &M->Journal[M->JournalExtents++];
    if (!Allocate (M, Count, &Extent->Start)) {
      return FALSE;
    }
    Extent->Count = Count;
    Journal      -= Count;
  }
  return TRUE;
}

STATIC UINT32
UsedIn (
  IN CONST MAKE *M,
  IN UINT32      Group
)
{
  UINT64 Start = GroupStart (Group);
  UINT32 Count = 0;

  for (UINT32 Index = 0; Index < GroupLen (M, Group); Index++) {
    Count += Used (M, Start + Index);
  }
  return Count;
}

STATIC BOOLEAN
InGroup (
  IN UINT64 Block,
  IN UINT32 Group
)
{
  return Block >= GroupStart (Group) && Block < GroupStart (Group + 1);
}

// What the kernel marks in an uninitialised group's bitmap: the backup
// superblock and descriptors, and the group's own metadata if it lies in
// the group.
STATIC UINT32
KernelUsed (
  IN CONST MAKE *M,
  IN UINT32      Group
)
{
  UINT32 Count = BaseMeta (M, Group);

  Count += InGroup (M->BlockBitmap[Group], Group);
  Count += InGroup (M->InodeBitmap[Group], Group);
  for (UINT32 Index = 0; Index < M->Itb; Index++) {
    Count += InGroup (M->InodeTable[Group] + Index, Group);
  }
  return Count;
}

// The inode bitmap of a group: inodes 1 to 11 used in group 0, padding
// past the group's inodes set.
STATIC VOID
InodeBitmap (
  IN CONST MAKE *M,
  IN UINT32      Group,
  OUT UINT8     *Out
)
{
  ZeroMem (Out, BLOCK);
  if (Group == 0) {
    for (UINT32 Bit = 0; Bit < FIRST_INO; Bit++) {
      Out[Bit / 8] |= (UINT8)(1 << (Bit % 8));
    }
  }
  SetMem (Out + M->Ipg / 8, BLOCK - M->Ipg / 8, 0xFF);
}

// Whether the group needs its block bitmap written: group 0, the last
// group, and any holding more than the kernel works out.
STATIC BOOLEAN
BlockInit (
  IN CONST MAKE *M,
  IN UINT32      Group
)
{
  return Group == 0 || Group + 1 == M->Groups ||
         UsedIn (M, Group) != KernelUsed (M, Group);
}

STATIC VOID
Descriptors (IN OUT MAKE *M)
{
  UINT8 Inodes[BLOCK];

  M->FreeBlocks = 0;
  for (UINT32 Group = 0; Group < M->Groups; Group++) {
    UINT8  *Desc  = M->Gdt + (UINTN)Group * DESC;
    UINT32  Free  = GroupLen (M, Group) - UsedIn (M, Group);
    UINT32  Taken = Group == 0 ? FIRST_INO : 0;
    UINT16  Flags = Group == 0 ? 0 : BG_INODE_UNINIT;
    UINT32  Crc;
    UINT8   Zero[2] = {0, 0};

    if (!BlockInit (M, Group)) {
      Flags |= BG_BLOCK_UNINIT;
    }
    M->FreeBlocks += Free;
    InodeBitmap (M, Group, Inodes);
    WriteLe32 (Desc + 0x00, (UINT32)M->BlockBitmap[Group]);
    WriteLe32 (Desc + 0x04, (UINT32)M->InodeBitmap[Group]);
    WriteLe32 (Desc + 0x08, (UINT32)M->InodeTable[Group]);
    WriteLe16 (Desc + 0x0C, (UINT16)Free);
    WriteLe16 (Desc + 0x0E, (UINT16)(M->Ipg - Taken));
    WriteLe16 (Desc + 0x10, Group == 0 ? 2 : 0);
    WriteLe16 (Desc + 0x12, Flags);
    Crc = Crc32c (M->Seed, M->Bitmap + (UINTN)Group * BLOCK, PER_GROUP / 8);
    WriteLe16 (Desc + 0x18, (UINT16)Crc);
    WriteLe16 (Desc + 0x38, (UINT16)(Crc >> 16));
    Crc = Crc32c (M->Seed, Inodes, M->Ipg / 8);
    WriteLe16 (Desc + 0x1A, (UINT16)Crc);
    WriteLe16 (Desc + 0x3A, (UINT16)(Crc >> 16));
    WriteLe16 (Desc + 0x1C, (UINT16)(M->Ipg - Taken));
    WriteLe32 (Desc + 0x20, (UINT32)(M->BlockBitmap[Group] >> 32));
    WriteLe32 (Desc + 0x24, (UINT32)(M->InodeBitmap[Group] >> 32));
    WriteLe32 (Desc + 0x28, (UINT32)(M->InodeTable[Group] >> 32));
    // Over the group number and the descriptor, its checksum as zero.
    Crc = Crc32cLe32 (M->Seed, Group);
    Crc = Crc32c (Crc, Desc, 0x1E);
    Crc = Crc32c (Crc, Zero, 2);
    Crc = Crc32c (Crc, Desc + 0x20, DESC - 0x20);
    WriteLe16 (Desc + 0x1E, (UINT16)Crc);
  }
}

// Fills an inode with up to four extents, and its checksum.
STATIC VOID
PutInode (
  IN CONST MAKE   *M,
  OUT UINT8       *Inode,
  IN UINT32        Number,
  IN UINT16        Mode,
  IN UINT16        Links,
  IN CONST EXTENT *Extents,
  IN UINT32        Count
)
{
  UINT32 Time   = M->Params->Time;
  UINT64 Blocks = 0;
  UINT32 Logical = 0;
  UINT32 Crc;

  ZeroMem (Inode, INODE);
  for (UINT32 Index = 0; Index < Count; Index++) {
    UINT8 *Extent = Inode + 0x28 + 12 + 12 * Index;
    WriteLe32 (Extent, Logical);
    WriteLe16 (Extent + 4, (UINT16)Extents[Index].Count);
    WriteLe16 (Extent + 6, (UINT16)(Extents[Index].Start >> 32));
    WriteLe32 (Extent + 8, (UINT32)Extents[Index].Start);
    Logical += Extents[Index].Count;
    Blocks  += Extents[Index].Count;
  }
  WriteLe16 (Inode + 0x00, Mode);
  WriteLe32 (Inode + 0x04, (UINT32)(Blocks * BLOCK));
  WriteLe32 (Inode + 0x08, Time);
  WriteLe32 (Inode + 0x0C, Time);
  WriteLe32 (Inode + 0x10, Time);
  WriteLe16 (Inode + 0x1A, Links);
  WriteLe32 (Inode + 0x1C, (UINT32)(Blocks * (BLOCK / 512)));
  WriteLe32 (Inode + 0x20, EXTENTS_FL);
  // Extent header: magic, entries, room for four, depth 0.
  WriteLe16 (Inode + 0x28, 0xF30A);
  WriteLe16 (Inode + 0x2A, (UINT16)Count);
  WriteLe16 (Inode + 0x2C, MAX_EXTENTS);
  WriteLe32 (Inode + 0x6C, (UINT32)((Blocks * BLOCK) >> 32));
  WriteLe16 (Inode + 0x80, EXTRA_ISIZE);
  WriteLe32 (Inode + 0x90, Time);
  // Seeded with the inode number and generation 0, over the inode with
  // both checksum halves zero.
  Crc = Crc32cLe32 (Crc32cLe32 (M->Seed, Number), 0);
  Crc = Crc32c (Crc, Inode, INODE);
  WriteLe16 (Inode + 0x7C, (UINT16)Crc);
  WriteLe16 (Inode + 0x82, (UINT16)(Crc >> 16));
}

// Adds a directory entry at *At with the given record length.
STATIC VOID
PutDirent (
  IN OUT UINT8   *Block,
  IN OUT UINTN   *At,
  IN UINT32       Inode,
  IN UINT16       Length,
  IN CONST CHAR8 *Name
)
{
  UINTN Len = AsciiStrLen (Name);

  WriteLe32 (Block + *At, Inode);
  WriteLe16 (Block + *At + 4, Length);
  Block[*At + 6] = (UINT8)Len;
  Block[*At + 7] = Inode ? 2 : 0;
  CopyMem (Block + *At + 8, Name, Len);
  *At += Length;
}

// Ends a directory block with its checksum tail.
STATIC VOID
DirTail (
  IN CONST MAKE *M,
  IN OUT UINT8  *Block,
  IN UINT32      Inode
)
{
  UINT8 *Tail = Block + BLOCK - TAIL;
  UINT32 Crc  = Crc32cLe32 (Crc32cLe32 (M->Seed, Inode), 0);

  WriteLe16 (Tail + 4, TAIL);
  Tail[7] = 0xDE;
  WriteLe32 (Tail + 8, Crc32c (Crc, Block, BLOCK - TAIL));
}

STATIC VOID
SuperBlock (
  IN CONST MAKE *M,
  IN UINT32      Group,
  OUT UINT8     *Sb
)
{
  CONST EXT4_MAKE *P      = M->Params;
  UINT32           Inodes = M->Ipg * M->Groups;
  UINT8            Journal[INODE];

  ZeroMem (Sb, 1024);
  WriteLe32 (Sb + 0x00, Inodes);
  WriteLe32 (Sb + 0x04, (UINT32)M->Blocks);
  WriteLe32 (Sb + 0x0C, (UINT32)M->FreeBlocks);
  WriteLe32 (Sb + 0x10, Inodes - FIRST_INO);
  WriteLe32 (Sb + 0x18, 2);
  WriteLe32 (Sb + 0x1C, 2);
  WriteLe32 (Sb + 0x20, PER_GROUP);
  WriteLe32 (Sb + 0x24, PER_GROUP);
  WriteLe32 (Sb + 0x28, M->Ipg);
  WriteLe32 (Sb + 0x30, P->Time);
  WriteLe16 (Sb + 0x36, 0xFFFF);
  WriteLe16 (Sb + 0x38, 0xEF53);
  WriteLe16 (Sb + 0x3A, 1);
  WriteLe16 (Sb + 0x3C, 1);
  WriteLe32 (Sb + 0x40, P->Time);
  WriteLe32 (Sb + 0x4C, 1);
  WriteLe32 (Sb + 0x54, FIRST_INO);
  WriteLe16 (Sb + 0x58, INODE);
  WriteLe16 (Sb + 0x5A, (UINT16)Group);
  WriteLe32 (Sb + 0x5C, COMPAT);
  WriteLe32 (Sb + 0x60, INCOMPAT);
  WriteLe32 (Sb + 0x64, RO_COMPAT);
  CopyMem (Sb + 0x68, P->Uuid, 16);
  CopyMem (Sb + 0x78, P->Label, AsciiStrLen (P->Label));
  WriteLe32 (Sb + 0xE0, JOURNAL_INO);
  CopyMem (Sb + 0xEC, P->HashSeed, 16);
  Sb[0xFC] = 1; // half_md4
  Sb[0xFD] = 1; // s_jnl_blocks holds the journal's extents
  WriteLe16 (Sb + 0xFE, DESC);
  WriteLe32 (Sb + 0x100, 0x000C); // user_xattr, acl
  WriteLe32 (Sb + 0x108, P->Time);
  // The journal inode's block map and size, for e2fsck.
  PutInode (
    M, Journal, JOURNAL_INO, MODE_FILE | 0600, 1, M->Journal,
    M->JournalExtents
  );
  CopyMem (Sb + 0x10C, Journal + 0x28, 60);
  CopyMem (Sb + 0x10C + 60, Journal + 0x6C, 4);
  CopyMem (Sb + 0x10C + 64, Journal + 0x04, 4);
  WriteLe16 (Sb + 0x15C, EXTRA_ISIZE);
  WriteLe16 (Sb + 0x15E, EXTRA_ISIZE);
  WriteLe32 (Sb + 0x160, 0x0002); // unsigned directory hash
  Sb[0x174] = LOG_FLEX;
  Sb[0x175] = 1; // crc32c
  // metadata_csum_seed: the UUID can change without new checksums.
  WriteLe32 (Sb + 0x270, M->Seed);
  WriteLe32 (Sb + 0x3FC, Crc32c (~0u, Sb, 0x3FC));
}

STATIC BOOLEAN
WriteBlock (
  IN CONST MAKE  *M,
  IN UINT64       Block,
  IN CONST UINT8 *Data,
  IN UINTN        Size
)
{
  return M->Write (Block * BLOCK, Data, Size, M->Context);
}

// Group 0's first inode table block, the root and lost+found, and the
// journal superblock.
STATIC BOOLEAN
WriteFiles (
  IN CONST MAKE *M,
  IN OUT UINT8  *Block
)
{
  EXTENT Root = {M->Root, 1};
  EXTENT Lost = {M->Lost, LOST_BLOCKS};
  UINTN  At;

  ZeroMem (Block, BLOCK);
  PutInode (M, Block + (ROOT_INO - 1) * INODE, ROOT_INO, MODE_DIR | 0755, 3,
            &Root, 1);
  PutInode (
    M, Block + (JOURNAL_INO - 1) * INODE, JOURNAL_INO, MODE_FILE | 0600, 1,
    M->Journal, M->JournalExtents
  );
  PutInode (M, Block + (LOST_INO - 1) * INODE, LOST_INO, MODE_DIR | 0700, 2,
            &Lost, 1);
  if (!WriteBlock (M, M->InodeTable[0], Block, BLOCK)) {
    return FALSE;
  }
  ZeroMem (Block, BLOCK);
  At = 0;
  PutDirent (Block, &At, ROOT_INO, 12, ".");
  PutDirent (Block, &At, ROOT_INO, 12, "..");
  PutDirent (Block, &At, LOST_INO, (UINT16)(BLOCK - TAIL - At), "lost+found");
  DirTail (M, Block, ROOT_INO);
  if (!WriteBlock (M, M->Root, Block, BLOCK)) {
    return FALSE;
  }
  for (UINT32 Index = 0; Index < LOST_BLOCKS; Index++) {
    ZeroMem (Block, BLOCK);
    At = 0;
    if (Index == 0) {
      PutDirent (Block, &At, LOST_INO, 12, ".");
      PutDirent (Block, &At, ROOT_INO, (UINT16)(BLOCK - TAIL - At), "..");
    } else {
      PutDirent (Block, &At, 0, BLOCK - TAIL, "");
    }
    DirTail (M, Block, LOST_INO);
    if (!WriteBlock (M, M->Lost + Index, Block, BLOCK)) {
      return FALSE;
    }
  }
  // The journal superblock, big-endian: empty, starting at sequence 1.
  ZeroMem (Block, BLOCK);
  WriteBe32 (Block + 0x00, 0xC03B3998);
  WriteBe32 (Block + 0x04, 4);
  WriteBe32 (Block + 0x0C, BLOCK);
  WriteBe32 (Block + 0x10, M->Params->JournalBlocks);
  WriteBe32 (Block + 0x14, 1);
  WriteBe32 (Block + 0x18, 1);
  CopyMem (Block + 0x30, M->Params->Uuid, 16);
  WriteBe32 (Block + 0x40, 1);
  return WriteBlock (M, M->Journal[0].Start, Block, BLOCK);
}

STATIC BOOLEAN
WriteAll (IN OUT MAKE *M)
{
  UINT8  *Block = AllocateZeroPool (BLOCK);
  UINTN   GdtSize = (UINTN)M->GdtBlocks * BLOCK;
  BOOLEAN Ok    = FALSE;

  if (!Block) {
    return FALSE;
  }
  for (UINT32 Group = 0; Group < M->Groups; Group++) {
    if (BlockInit (M, Group) &&
        !WriteBlock (
          M, M->BlockBitmap[Group], M->Bitmap + (UINTN)Group * BLOCK, BLOCK
        )) {
      goto Done;
    }
  }
  InodeBitmap (M, 0, Block);
  if (!WriteBlock (M, M->InodeBitmap[0], Block, BLOCK) ||
      !WriteFiles (M, Block)) {
    goto Done;
  }
  // Backups, then the primary descriptors, then the primary superblock.
  for (UINT32 Group = 1; Group < M->Groups; Group++) {
    if (!HasSuper (Group)) {
      continue;
    }
    ZeroMem (Block, BLOCK);
    SuperBlock (M, Group, Block);
    if (!WriteBlock (M, GroupStart (Group), Block, BLOCK) ||
        !WriteBlock (M, GroupStart (Group) + 1, M->Gdt, GdtSize)) {
      goto Done;
    }
  }
  if (!WriteBlock (M, 1, M->Gdt, GdtSize)) {
    goto Done;
  }
  ZeroMem (Block, BLOCK);
  SuperBlock (M, 0, Block + 1024);
  Ok = WriteBlock (M, 0, Block, BLOCK);
Done:
  FreePool (Block);
  return Ok;
}

// Groups, inodes per group and descriptor blocks for M->Blocks. FALSE if
// the inodes don't fit.
STATIC BOOLEAN
Geometry (IN OUT MAKE *M)
{
  UINT64 Inodes = M->Blocks * BLOCK / M->Params->InodeRatio;
  UINT64 Ipg;

  M->Groups = (UINT32)((M->Blocks + PER_GROUP - 1) / PER_GROUP);
  Ipg       = (Inodes + M->Groups - 1) / M->Groups;
  Ipg       = (Ipg + PER_BLOCK - 1) / PER_BLOCK * PER_BLOCK;
  if (Ipg < PER_BLOCK) {
    Ipg = PER_BLOCK;
  }
  if (Ipg > BLOCK * 8 || Ipg * M->Groups > MAX_UINT32) {
    return FALSE;
  }
  M->Ipg       = (UINT32)Ipg;
  M->Itb       = M->Ipg / PER_BLOCK;
  M->GdtBlocks = (M->Groups * DESC + BLOCK - 1) / BLOCK;
  return TRUE;
}

BOOLEAN
Ext4Make (
  IN CONST EXT4_MAKE *Params,
  IN EXT4_WRITE      *Write,
  IN VOID            *Context
)
{
  MAKE    M = {0};
  UINT32  Rest;
  BOOLEAN Ok = FALSE;

  if (Params->Bytes < MIN_BYTES || Params->InodeRatio < 16384 ||
      (Params->InodeRatio & (Params->InodeRatio - 1)) ||
      Params->JournalBlocks < 1024 ||
      Params->JournalBlocks > MAX_EXTENTS * MAX_EXTENT ||
      AsciiStrLen (Params->Label) > 16) {
    return FALSE;
  }
  M.Params  = Params;
  M.Write   = Write;
  M.Context = Context;
  M.Blocks  = Params->Bytes / BLOCK;
  if (M.Blocks > MAX_BLOCKS) {
    return FALSE;
  }
  if (!Geometry (&M)) {
    return FALSE;
  }
  // As mke2fs: a short last group without room for its metadata and 50
  // blocks more is left out.
  Rest = (UINT32)(M.Blocks % PER_GROUP);
  if (M.Groups > 1 && Rest &&
      Rest < 3 + M.Itb + BaseMeta (&M, M.Groups - 1) + 50) {
    M.Blocks -= Rest;
    if (!Geometry (&M)) {
      return FALSE;
    }
  }
  M.Seed        = Crc32c (~0u, Params->Uuid, 16);
  M.Bitmap      = AllocateZeroArray (M.Groups, BLOCK);
  M.BlockBitmap = AllocateZeroArray (M.Groups, sizeof (UINT64));
  M.InodeBitmap = AllocateZeroArray (M.Groups, sizeof (UINT64));
  M.InodeTable  = AllocateZeroArray (M.Groups, sizeof (UINT64));
  M.Gdt         = AllocateZeroArray (M.GdtBlocks, BLOCK);
  if (M.Bitmap && M.BlockBitmap && M.InodeBitmap && M.InodeTable && M.Gdt &&
      Layout (&M)) {
    Descriptors (&M);
    Ok = WriteAll (&M);
  }
  FreePool (M.Bitmap);
  FreePool (M.BlockBitmap);
  FreePool (M.InodeBitmap);
  FreePool (M.InodeTable);
  FreePool (M.Gdt);
  return Ok;
}
