/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  GptTest - the install layouts on a small disk shaped like the Nova's:
  Android's partitions from super on, userdata last.
*/

#include <stdio.h>

#include "Base/Memory.h"
#include "Core/Gpt.h"
#include "Tests/Test.h"

#define BLOCK 4096u
// 128 MiB; the sizes are the Nova's scaled down.
#define DISK  32768u

STATIC CONST GPT_INSTALL mInstall = {
  .AndroidBlocks = 4000,
  .BootBlocks    = 2048,
  .MinStorage    = 1024,
  .Align         = 256,
  .BootGuid      = {0xb0, 0x07, [6] = 0x40, [8] = 0x80, [15] = 1},
  .StorageGuid   = {0x57, 0x0e, [6] = 0x40, [8] = 0x80, [15] = 2},
};

STATIC VOID
AddPart (
  IN OUT GPT_PART *Parts,
  IN OUT UINTN    *Count,
  IN CONST CHAR8  *Name,
  IN UINT64        Blocks
)
{
  Parts[*Count] = (GPT_PART){.Type = {0x42}, .Name = Name, .Blocks = Blocks};
  (*Count)++;
}

// A disk with misc, metadata, super, vbmeta_system_a and _b, Between if
// given, userdata filling the rest, then Extra after userdata if given.
STATIC BOOLEAN
NovaDisk (
  IN CONST CHAR8  *Between,
  IN CONST CHAR8  *Extra,
  OUT OABL_BUFFER *Disk
)
{
  GPT_PART Parts[8];
  UINTN    Count = 0;

  AddPart (Parts, &Count, "misc", 1);
  AddPart (Parts, &Count, "metadata", 16);
  AddPart (Parts, &Count, "super", 3001);
  AddPart (Parts, &Count, "vbmeta_system_a", 4);
  AddPart (Parts, &Count, "vbmeta_system_b", 4);
  if (Between) {
    AddPart (Parts, &Count, Between, 64);
  }
  AddPart (Parts, &Count, "userdata", Extra ? 20000 : 0);
  if (Extra) {
    AddPart (Parts, &Count, Extra, 0);
  }
  return GptBuild (DISK, BLOCK, Parts, Count, Disk);
}

// Applies Writes to Disk, then reads both tables back: they must verify
// and agree.
STATIC BOOLEAN
Apply (
  IN OUT OABL_BUFFER *Disk,
  IN CONST GPT_WRITES *Writes,
  OUT GPT_HEADER     *Header,
  OUT OABL_BYTES     *Array
)
{
  GPT_HEADER Backup;
  OABL_BYTES BackupArray;

  for (UINTN Index = 0; Index < Writes->Count; Index++) {
    CopyMem (
      Disk->Data + Writes->Items[Index].Lba * BLOCK,
      Writes->Items[Index].Data.Data, Writes->Items[Index].Data.Len
    );
  }
  return GptReadBack (Disk->Data, Disk->Len, BLOCK, 1, Header, Array) &&
         GptReadBack (
           Disk->Data, Disk->Len, BLOCK, Header->AlternateLba, &Backup,
           &BackupArray
         ) &&
         GptCopiesAgree (Header, &Backup, DISK - 1) &&
         CompareMem (Array->Ptr, BackupArray.Ptr, Array->Len) == 0;
}

STATIC BOOLEAN
Table (
  IN CONST OABL_BUFFER *Disk,
  OUT GPT_HEADER       *Header,
  OUT OABL_BYTES       *Array
)
{
  return GptReadBack (Disk->Data, Disk->Len, BLOCK, 1, Header, Array);
}

// Finds the entry called Name: its index, first and last block and type.
STATIC BOOLEAN
Find (
  IN CONST GPT_HEADER *Header,
  IN OABL_BYTES        Array,
  IN CONST CHAR8      *Name,
  OUT UINTN           *Index,
  OUT UINT64          *First,
  OUT UINT64          *Last,
  OUT UINT8            Type[16]
)
{
  for (UINTN At = 0; At < Header->Entries; At++) {
    if (GptEntryNamed (Header, Array.Ptr, Array.Len, At, Name)) {
      *Index = At;
      return GptExtent (Header, Array.Ptr, Array.Len, At, First, Last) &&
             GptEntryType (Header, Array.Ptr, Array.Len, At, Type);
    }
  }
  return FALSE;
}

STATIC VOID
Dualboot (VOID)
{
  OABL_BUFFER   Disk   = {0};
  GPT_WRITES    Writes = {0};
  GPT_HEADER    Header;
  OABL_BYTES    Array;
  GPT_SURVEY    Survey;
  GPT_PORTAREOS Where;
  UINT8         Type[16];
  UINTN         Index;
  UINT64        First;
  UINT64        Last;
  UINT64        UserdataFirst;

  CHECK (NovaDisk (NULL, NULL, &Disk));
  CHECK (Table (&Disk, &Header, &Array));
  GptSurvey (&Header, Array.Ptr, Array.Len, &Survey);
  CHECK (Survey.Android && !Survey.PortareOs && Survey.CanShrink &&
         Survey.CanReplace);
  UserdataFirst = Survey.UserdataFirst;
  CHECK (GptInstallDualboot (
    Disk.Data + BLOCK, BLOCK, Array.Ptr, Array.Len, BLOCK,
    Header.LastUsable + 1, &mInstall, &Writes, &Where
  ));
  // Userdata keeps at least what was asked; the boot partition is aligned.
  CHECK (Where.BootFirst % mInstall.Align == 0);
  CHECK (Where.BootFirst >= UserdataFirst + mInstall.AndroidBlocks);
  CHECK (Where.BootFirst < UserdataFirst + mInstall.AndroidBlocks +
                               mInstall.Align);
  CHECK (Where.BootLast - Where.BootFirst + 1 == mInstall.BootBlocks);
  CHECK (Where.StorageFirst == Where.BootLast + 1);
  CHECK (Where.StorageLast == Header.LastUsable);
  CHECK (Apply (&Disk, &Writes, &Header, &Array));
  GptWritesFree (&Writes);
  CHECK (Find (&Header, Array, "userdata", &Index, &First, &Last, Type));
  CHECK (First == UserdataFirst && Last == Where.BootFirst - 1);
  // Userdata keeps its own type.
  CHECK (Type[0] == 0x42);
  CHECK (Find (&Header, Array, GPT_BOOT_NAME, &Index, &First, &Last, Type));
  CHECK (First == Where.BootFirst && Last == Where.BootLast);
  CHECK (CompareMem (Type, gGptEfiSystem, 16) == 0);
  CHECK (Find (&Header, Array, GPT_STORAGE_NAME, &Index, &First, &Last, Type));
  CHECK (First == Where.StorageFirst && Last == Where.StorageLast);
  CHECK (CompareMem (Type, gGptLinuxFs, 16) == 0);
  GptSurvey (&Header, Array.Ptr, Array.Len, &Survey);
  CHECK (Survey.Android && Survey.PortareOs && !Survey.CanShrink &&
         Survey.CanReplace);
  CHECK (CompareMem (Survey.Found.BootGuid, mInstall.BootGuid, 16) == 0);
  CHECK (Survey.Found.BootFirst == Where.BootFirst);
  // A second Dualboot needs the layout as it is.
  CHECK (!GptInstallDualboot (
    Disk.Data + BLOCK, BLOCK, Array.Ptr, Array.Len, BLOCK,
    Header.LastUsable + 1, &mInstall, &Writes, &Where
  ));
  // The uninstaller takes it back out.
  CHECK (GptUninstall (
    Disk.Data + BLOCK, BLOCK, Array.Ptr, Array.Len, BLOCK,
    Header.LastUsable + 1, &Writes
  ));
  CHECK (Apply (&Disk, &Writes, &Header, &Array));
  GptWritesFree (&Writes);
  GptSurvey (&Header, Array.Ptr, Array.Len, &Survey);
  CHECK (Survey.Android && !Survey.PortareOs && Survey.CanShrink);
  CHECK (Survey.UserdataLast == Header.LastUsable);
  BufferFree (&Disk);
}

// Replace Android on a stock disk, or after Dualboot.
STATIC VOID
Replace (IN BOOLEAN AfterDualboot)
{
  OABL_BUFFER   Disk   = {0};
  GPT_WRITES    Writes = {0};
  GPT_HEADER    Header;
  OABL_BYTES    Array;
  GPT_SURVEY    Survey;
  GPT_PORTAREOS Where;
  UINT8         Type[16];
  UINTN         Index;
  UINT64        First;
  UINT64        Last;
  UINT64        SuperFirst;

  CHECK (NovaDisk (NULL, NULL, &Disk));
  CHECK (Table (&Disk, &Header, &Array));
  CHECK (Find (&Header, Array, "super", &Index, &SuperFirst, &Last, Type));
  if (AfterDualboot) {
    CHECK (GptInstallDualboot (
      Disk.Data + BLOCK, BLOCK, Array.Ptr, Array.Len, BLOCK,
      Header.LastUsable + 1, &mInstall, &Writes, &Where
    ));
    CHECK (Apply (&Disk, &Writes, &Header, &Array));
    GptWritesFree (&Writes);
  }
  CHECK (GptInstallReplace (
    Disk.Data + BLOCK, BLOCK, Array.Ptr, Array.Len, BLOCK,
    Header.LastUsable + 1, &mInstall, &Writes, &Where
  ));
  CHECK (Where.BootFirst == (SuperFirst + mInstall.Align - 1) /
                                mInstall.Align * mInstall.Align);
  CHECK (Where.StorageLast == Header.LastUsable);
  CHECK (Apply (&Disk, &Writes, &Header, &Array));
  GptWritesFree (&Writes);
  // Android's partitions are gone; what came before super stays.
  CHECK (!Find (&Header, Array, "super", &Index, &First, &Last, Type));
  CHECK (!Find (&Header, Array, "userdata", &Index, &First, &Last, Type));
  CHECK (!Find (&Header, Array, "vbmeta_system_a", &Index, &First, &Last, Type));
  CHECK (Find (&Header, Array, "metadata", &Index, &First, &Last, Type));
  CHECK (Find (&Header, Array, GPT_BOOT_NAME, &Index, &First, &Last, Type));
  // super's entry, the first freed, holds the boot partition.
  CHECK (Index == 2);
  GptSurvey (&Header, Array.Ptr, Array.Len, &Survey);
  CHECK (!Survey.Android && Survey.PortareOs && !Survey.CanShrink);
  CHECK (Survey.CanReplace && Survey.ReplaceFirst == Where.BootFirst);
  CHECK (!GptCfwAfterUserdata (&Header, Array.Ptr, Array.Len));
  // Replacing again starts over in the same place.
  CHECK (GptInstallReplace (
    Disk.Data + BLOCK, BLOCK, Array.Ptr, Array.Len, BLOCK,
    Header.LastUsable + 1, &mInstall, &Writes, &Where
  ));
  CHECK (Where.BootFirst == Survey.ReplaceFirst);
  GptWritesFree (&Writes);
  BufferFree (&Disk);
}

// Nothing is written over a partition the installs don't know.
STATIC VOID
Refusals (VOID)
{
  OABL_BUFFER   Disk   = {0};
  GPT_WRITES    Writes = {0};
  GPT_HEADER    Header;
  OABL_BYTES    Array;
  GPT_SURVEY    Survey;
  GPT_PORTAREOS Where;
  GPT_INSTALL   Install = mInstall;

  // Something after userdata: neither install.
  CHECK (NovaDisk (NULL, "cache", &Disk));
  CHECK (Table (&Disk, &Header, &Array));
  GptSurvey (&Header, Array.Ptr, Array.Len, &Survey);
  CHECK (Survey.Android && !Survey.CanShrink && !Survey.CanReplace);
  CHECK (!GptInstallDualboot (
    Disk.Data + BLOCK, BLOCK, Array.Ptr, Array.Len, BLOCK,
    Header.LastUsable + 1, &Install, &Writes, &Where
  ));
  CHECK (!GptInstallReplace (
    Disk.Data + BLOCK, BLOCK, Array.Ptr, Array.Len, BLOCK,
    Header.LastUsable + 1, &Install, &Writes, &Where
  ));
  BufferFree (&Disk);
  // Something between super and userdata: Dualboot only.
  CHECK (NovaDisk ("cache", NULL, &Disk));
  CHECK (Table (&Disk, &Header, &Array));
  GptSurvey (&Header, Array.Ptr, Array.Len, &Survey);
  CHECK (Survey.CanShrink && !Survey.CanReplace);
  // Android asking for all of userdata, or storage too small.
  Install.AndroidBlocks = Survey.UserdataLast - Survey.UserdataFirst + 1;
  CHECK (!GptInstallDualboot (
    Disk.Data + BLOCK, BLOCK, Array.Ptr, Array.Len, BLOCK,
    Header.LastUsable + 1, &Install, &Writes, &Where
  ));
  Install.AndroidBlocks = Survey.UserdataLast - Survey.UserdataFirst -
                          mInstall.BootBlocks - 512;
  CHECK (!GptInstallDualboot (
    Disk.Data + BLOCK, BLOCK, Array.Ptr, Array.Len, BLOCK,
    Header.LastUsable + 1, &Install, &Writes, &Where
  ));
  BufferFree (&Disk);
}

INT32
GptTests (VOID)
{
  INT32 Before = gTestFailures;

  Dualboot ();
  Replace (FALSE);
  Replace (TRUE);
  Refusals ();
  printf ("gpt-test: %s\n", gTestFailures == Before ? "passed" : "FAILED");
  return gTestFailures - Before;
}
