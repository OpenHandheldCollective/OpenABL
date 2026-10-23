/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  MakeTest - writes images with Fat32Make and Ext4Make for Tests/check.sh,
  which checks them with fsck.fat, mtools, e2fsck and debugfs.

  Usage: make-test DIR. Writes DIR/fat-*.img with the files it should
  hold under DIR/fat-*.files, and DIR/ext4-*.img with DIR/ext4-*.txt
  holding the size and journal blocks. The FAT images are also read back
  here with Core/Fat32.
*/

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Core/Ext4Make.h"
#include "Core/Fat32.h"
#include "Core/Fat32Make.h"

STATIC INT32 mFailures;

#define CHECK(Cond)                                              \
  do {                                                           \
    if (!(Cond)) {                                               \
      fprintf (stderr, "%s:%d: %s\n", __FILE__, __LINE__, #Cond); \
      mFailures++;                                               \
    }                                                            \
  } while (0)

STATIC BOOLEAN
FileWrite (
  IN UINT64       Offset,
  IN CONST UINT8 *Buffer,
  IN UINTN        Size,
  IN VOID        *Context
)
{
  return pwrite (*(int *)Context, Buffer, Size, (off_t)Offset) == (ssize_t)Size;
}

STATIC BOOLEAN
FileRead (
  IN UINT64  Offset,
  OUT UINT8 *Buffer,
  IN UINTN   Size,
  IN VOID   *Context
)
{
  return pread (*(int *)Context, Buffer, Size, (off_t)Offset) == (ssize_t)Size;
}

// A sparse image of Bytes, filled with Fill where nothing is written, to
// stand in for stale data on the disk.
STATIC int
NewImage (
  IN CONST char *Path,
  IN UINT64      Bytes,
  IN UINT8       Fill
)
{
  int Fd = open (Path, O_RDWR | O_CREAT | O_TRUNC, 0644);

  if (Fd < 0 || ftruncate (Fd, (off_t)Bytes) != 0) {
    perror (Path);
    exit (1);
  }
  if (Fill) {
    // Stale bytes over the first 64 MiB, where the metadata goes.
    UINT8 Chunk[65536];
    memset (Chunk, Fill, sizeof (Chunk));
    for (UINT64 At = 0; At < (64ull << 20) && At < Bytes; At += sizeof (Chunk)) {
      CHECK (FileWrite (At, Chunk, sizeof (Chunk), &Fd));
    }
  }
  return Fd;
}

// The content of a test file: a byte stream seeded by its index.
STATIC UINT8
Content (
  IN UINTN  File,
  IN UINT64 At
)
{
  UINT64 X = (At / 8 + 1) * 0x9E3779B97F4A7C15ull ^ (File + 1) * 0xD1B54A32D192ED03ull;

  X ^= X >> 29;
  X *= 0xBF58476D1CE4E5B9ull;
  X ^= X >> 32;
  return (UINT8)(X >> (8 * (At % 8)));
}

typedef struct {
  CONST char *Path; // as mtools names it, from the root
  UINT64      Size;
  BOOLEAN     Dir;
} SPEC;

// PortareOS's boot files, and names that need long-name entries, clash
// in their short form, or fill an entry exactly.
STATIC CONST SPEC mFiles[] = {
  {"KERNEL", 17664000, FALSE},
  {"SYSTEM", 43490099, FALSE},
  {"KERNEL.md5", 48, FALSE},
  {"SYSTEM.md5", 48, FALSE},
  {"portareos_abl", 0, TRUE},
  {"portareos_abl/README", 427, FALSE},
  {"portareos_abl/abl_signed-SM8550.elf", 258048, FALSE},
  {"portareos_abl/backup_abl.sh", 187, FALSE},
  {"portareos_abl/flash_abl.sh", 183, FALSE},
  {"portareos_abl/restore_backup_abl.sh", 159, FALSE},
  {"fs-resize.log", 1082, FALSE},
  {"EMPTY", 0, FALSE},
  {"long file name one.txt", 5000, FALSE},
  {"long file name two.txt", 5001, FALSE},
  {"thirteen12345", 4096, FALSE},
  {"twenty-six-characters-abc", 1, FALSE},
  {"a", 0, TRUE},
  {"a/b", 0, TRUE},
  {"a/b/deep file.bin", 70000, FALSE},
  {".hidden", 3, FALSE},
};
#define FILE_COUNT (sizeof (mFiles) / sizeof (mFiles[0]))

STATIC CHAR16 *
Utf16Of (IN CONST char *Ascii)
{
  UINTN   Len = strlen (Ascii);
  CHAR16 *Out = calloc (Len + 1, sizeof (CHAR16));

  for (UINTN Index = 0; Index < Len; Index++) {
    Out[Index] = (CHAR16)Ascii[Index];
  }
  return Out;
}

STATIC VOID
FatImage (
  IN CONST char *Dir,
  IN CONST char *Name,
  IN UINT64      Bytes,
  IN UINT32      SectorSize
)
{
  char          Path[512];
  FAT32_NODE    Nodes[FILE_COUNT];
  FAT32_PLAN    Plan = {0};
  int           Fd;
  FILE         *Out;
  UINT8        *Buffer = malloc (1 << 20);
  FAT32_VOLUME  Volume;
  FAT32_FILE    File;

  memset (Nodes, 0, sizeof (Nodes));
  for (UINTN Index = 0; Index < FILE_COUNT; Index++) {
    CONST char *Slash = strrchr (mFiles[Index].Path, '/');
    Nodes[Index].Name   = Utf16Of (Slash ? Slash + 1 : mFiles[Index].Path);
    Nodes[Index].Size   = mFiles[Index].Size;
    Nodes[Index].Dir    = mFiles[Index].Dir;
    Nodes[Index].Date   = (UINT16)((46 << 9) | (10 << 5) | 23);
    Nodes[Index].Time   = (UINT16)((21 << 11) | (7 << 5));
    Nodes[Index].Parent = -1;
    if (Slash) {
      UINTN Len = (UINTN)(Slash - mFiles[Index].Path);
      for (UINTN Other = 0; Other < Index; Other++) {
        if (strlen (mFiles[Other].Path) == Len &&
            strncmp (mFiles[Other].Path, mFiles[Index].Path, Len) == 0) {
          Nodes[Index].Parent = (INTN)Other;
        }
      }
    }
  }
  Plan.Bytes         = Bytes;
  Plan.SectorSize    = SectorSize;
  Plan.HiddenSectors = 3979264;
  Plan.Serial        = 0x12345678;
  strcpy (Plan.Label, "PORTAREOS");
  Plan.Nodes = Nodes;
  Plan.Count = FILE_COUNT;
  CHECK (Fat32MakePlan (&Plan));

  snprintf (Path, sizeof (Path), "%s/%s.files", Dir, Name);
  CHECK (mkdir (Path, 0755) == 0);
  snprintf (Path, sizeof (Path), "%s/%s.img", Dir, Name);
  Fd = NewImage (Path, Bytes, 0xA5);
  for (UINTN Index = 0; Index < FILE_COUNT; Index++) {
    UINT64 Offset = Fat32MakeOffset (&Plan, Nodes[Index].Cluster);
    if (Nodes[Index].Dir || Nodes[Index].Size == 0) {
      continue;
    }
    for (UINT64 At = 0; At < Nodes[Index].Size; At += 1 << 20) {
      UINTN Size = Nodes[Index].Size - At < (1 << 20) ? (UINTN)(Nodes[Index].Size - At)
                                                      : (1 << 20);
      for (UINTN Byte = 0; Byte < Size; Byte++) {
        Buffer[Byte] = Content (Index, At + Byte);
      }
      CHECK (FileWrite (Offset + At, Buffer, Size, &Fd));
    }
  }
  CHECK (Fat32MakeWrite (&Plan, FileWrite, &Fd));

  // Read back what Core/Fat32 can find: the 8.3 names.
  CHECK (Fat32Mount (&Volume, SectorSize, FileRead, &Fd));
  CHECK (memcmp (Volume.Label, "PORTAREOS  ", 11) == 0);
  CHECK (Fat32Find (&Volume, "KERNEL", &File) && File.Size == 17664000);
  CHECK (Fat32Find (&Volume, "PORTAR~1\\README", &File) && File.Size == 427);
  if (Fat32Find (&Volume, "SYSTEM", &File)) {
    OABL_BUFFER Data = {0};
    CHECK (Fat32Read (&Volume, &File, 0, (UINTN)File.Size, &Data));
    CHECK (Data.Len == mFiles[1].Size);
    for (UINTN Byte = 0; Byte < Data.Len; Byte += 4093) {
      if (Data.Data[Byte] != Content (1, Byte)) {
        CHECK (!"SYSTEM reads back");
        break;
      }
    }
    BufferFree (&Data);
  } else {
    CHECK (!"SYSTEM found");
  }
  Fat32Free (&Volume);
  close (Fd);

  // The expected tree, for diff against what mtools copies out.
  for (UINTN Index = 0; Index < FILE_COUNT; Index++) {
    snprintf (Path, sizeof (Path), "%s/%s.files/%s", Dir, Name, mFiles[Index].Path);
    if (mFiles[Index].Dir) {
      CHECK (mkdir (Path, 0755) == 0);
      continue;
    }
    Out = fopen (Path, "wb");
    for (UINT64 At = 0; At < mFiles[Index].Size; At++) {
      fputc (Content (Index, At), Out);
    }
    fclose (Out);
  }
  for (UINTN Index = 0; Index < FILE_COUNT; Index++) {
    free ((VOID *)Nodes[Index].Name);
  }
  free (Buffer);
}

// Names Fat32MakePlan must refuse.
STATIC VOID
FatRefusals (VOID)
{
  STATIC CONST char *Clashes[][2] = {
    {"Kernel", "KERNEL"},
    {"..", "x"},
    {".", "x"},
  };
  for (UINTN Case = 0; Case < sizeof (Clashes) / sizeof (Clashes[0]); Case++) {
    FAT32_NODE Nodes[2] = {{0}};
    FAT32_PLAN Plan     = {0};
    Nodes[0].Name   = Utf16Of (Clashes[Case][0]);
    Nodes[1].Name   = Utf16Of (Clashes[Case][1]);
    Nodes[0].Parent = Nodes[1].Parent = -1;
    Plan.Bytes      = 512ull << 20;
    Plan.SectorSize = 4096;
    Plan.Nodes      = Nodes;
    Plan.Count      = 2;
    CHECK (!Fat32MakePlan (&Plan));
    free ((VOID *)Nodes[0].Name);
    free ((VOID *)Nodes[1].Name);
  }
  {
    // Too few clusters for FAT32, then too much to fit.
    FAT32_NODE Node = {0};
    FAT32_PLAN Plan = {0};
    Node.Name   = Utf16Of ("BIG");
    Node.Parent = -1;
    Plan.SectorSize = 4096;
    Plan.Nodes      = &Node;
    Plan.Count      = 1;
    Plan.Bytes      = 200ull << 20;
    CHECK (!Fat32MakePlan (&Plan));
    Plan.Bytes = 512ull << 20;
    Node.Size  = 600ull << 20;
    CHECK (!Fat32MakePlan (&Plan));
    Node.Size = 4ull << 30;
    Plan.Bytes = 8ull << 30;
    CHECK (!Fat32MakePlan (&Plan));
    free ((VOID *)Node.Name);
  }
}

STATIC VOID
Ext4Image (
  IN CONST char *Dir,
  IN CONST char *Name,
  IN UINT64      Bytes,
  IN UINT32      JournalBlocks
)
{
  char      Path[512];
  EXT4_MAKE Params = {0};
  int       Fd;
  FILE     *Info;

  Params.Bytes         = Bytes;
  Params.InodeRatio    = 32768;
  Params.JournalBlocks = JournalBlocks;
  for (UINTN Index = 0; Index < 16; Index++) {
    Params.Uuid[Index]     = (UINT8)(0x40 + Index * 7);
    Params.HashSeed[Index] = (UINT8)(0x90 + Index * 3);
  }
  // A version 4 UUID.
  Params.Uuid[6] = (UINT8)((Params.Uuid[6] & 0x0F) | 0x40);
  Params.Uuid[8] = (UINT8)((Params.Uuid[8] & 0x3F) | 0x80);
  strcpy (Params.Label, "PORTAREOS_STOR");
  Params.Time = 1792800000;
  snprintf (Path, sizeof (Path), "%s/%s.img", Dir, Name);
  Fd = NewImage (Path, Bytes, 0x5A);
  CHECK (Ext4Make (&Params, FileWrite, &Fd));
  close (Fd);
  snprintf (Path, sizeof (Path), "%s/%s.txt", Dir, Name);
  Info = fopen (Path, "w");
  fprintf (Info, "%llu %u\n", (unsigned long long)Bytes, JournalBlocks);
  fclose (Info);
}

STATIC VOID
Ext4Refusals (VOID)
{
  EXT4_MAKE Params = {0};
  int       Fd     = -1;

  Params.Bytes         = 32ull << 20;
  Params.InodeRatio    = 32768;
  Params.JournalBlocks = 1024;
  CHECK (!Ext4Make (&Params, FileWrite, &Fd));
  Params.Bytes      = 1ull << 30;
  Params.InodeRatio = 30000;
  CHECK (!Ext4Make (&Params, FileWrite, &Fd));
  Params.InodeRatio    = 32768;
  Params.JournalBlocks = 512;
  CHECK (!Ext4Make (&Params, FileWrite, &Fd));
}

int
main (
  int   Argc,
  char *Argv[]
)
{
  if (Argc != 2) {
    fprintf (stderr, "usage: %s DIR\n", Argv[0]);
    return 2;
  }
  FatRefusals ();
  FatImage (Argv[1], "fat-4k", 2ull << 30, 4096);
  FatImage (Argv[1], "fat-512", 512ull << 20, 512);
  Ext4Refusals ();
  Ext4Image (Argv[1], "ext4-64m", 64ull << 20, 1024);
  Ext4Image (Argv[1], "ext4-300m", 300ull << 20, 4096);
  // A remainder group kept, then one dropped.
  Ext4Image (Argv[1], "ext4-1g", (1ull << 30) + (100ull << 20), 32768);
  Ext4Image (Argv[1], "ext4-1g-short", (1ull << 30) + (20ull << 20), 32768);
  // Over sixteen groups, so a second flex group; and a four-extent journal.
  Ext4Image (Argv[1], "ext4-5g", 5ull << 30, 131072);
  // The Nova's storage partition after Dualboot on this test device.
  Ext4Image (Argv[1], "ext4-nova", 100183ull << 20, 32768);
  if (mFailures) {
    fprintf (stderr, "%d failures\n", mFailures);
    return 1;
  }
  printf ("make-test: images written\n");
  return 0;
}
