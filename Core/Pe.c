/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Pe - Minimal PE/COFF parsing for Unified Kernel Image metadata.
*/

#include "Core/Pe.h"

#include "Base/Text.h"

VOID
PeSectionDataRange (
  IN CONST PE_SECTION *Section,
  OUT UINT64          *Offset,
  OUT UINTN           *Length
)
{
  *Offset = Section->FileOffset;
  *Length = Section->VirtualSize < Section->FileSize ? Section->VirtualSize
                                                     : Section->FileSize;
}

// Finds the COFF file header. Fails if Head isn't a PE image.
STATIC BOOLEAN
CoffHeader (
  IN CONST UINT8 *Head,
  IN UINTN        Size,
  OUT UINTN      *CoffOffset
)
{
  UINT32 PeOffset;

  if (Size < 2 || Head[0] != 'M' || Head[1] != 'Z') {
    return FALSE;
  }
  if (!GetLe32 (Head, Size, 0x3c, &PeOffset) || !InBounds (Size, PeOffset, 4)) {
    return FALSE;
  }
  if (Head[PeOffset] != 'P' || Head[PeOffset + 1] != 'E' ||
      Head[PeOffset + 2] || Head[PeOffset + 3]) {
    return FALSE;
  }
  *CoffOffset = (UINTN)PeOffset + 4;
  return TRUE;
}

BOOLEAN
PeIsPe (
  IN CONST UINT8 *Head,
  IN UINTN        Size
)
{
  UINTN CoffOffset;
  return CoffHeader (Head, Size, &CoffOffset);
}

BOOLEAN
PeMachine (
  IN CONST UINT8 *Head,
  IN UINTN        Size,
  OUT UINT16     *Machine
)
{
  UINTN CoffOffset;
  return CoffHeader (Head, Size, &CoffOffset) &&
         GetLe16 (Head, Size, CoffOffset, Machine);
}

BOOLEAN
PeSections (
  IN CONST UINT8  *Head,
  IN UINTN         Size,
  OUT PE_SECTIONS *Out
)
{
  UINT16 Count;
  UINT16 Optional;
  UINTN  CoffOffset;
  UINTN  At;

  *Out = (PE_SECTIONS){0};
  if (!CoffHeader (Head, Size, &CoffOffset) ||
      !GetLe16 (Head, Size, CoffOffset + 2, &Count) ||
      !GetLe16 (Head, Size, CoffOffset + 16, &Optional)) {
    return FALSE;
  }
  At = CoffOffset + 20 + Optional;
  for (UINT32 Index = 0; Index < Count; Index++, At += 40) {
    PE_SECTION   Section = {0};
    CONST UINT8 *Entry   = Head + At;
    UINTN        NameLen = 0;

    if (!InBounds (Size, At, 40)) {
      goto Fail;
    }
    // Each byte up to a NUL is a Latin-1 character. Store it as UTF-8.
    for (UINTN Index2 = 0; Index2 < 8 && Entry[Index2]; Index2++) {
      if (Entry[Index2] < 0x80) {
        Section.Name[NameLen++] = (CHAR8)Entry[Index2];
      } else {
        Section.Name[NameLen++] = (CHAR8)(0xc0 | Entry[Index2] >> 6);
        Section.Name[NameLen++] = (CHAR8)(0x80 | (Entry[Index2] & 0x3f));
      }
    }
    Section.VirtualSize = ReadLe32 (Entry + 8);
    Section.FileSize    = ReadLe32 (Entry + 16);
    Section.FileOffset  = ReadLe32 (Entry + 20);
    if (!VecPush (Out, Section)) {
      goto Fail;
    }
  }
  return TRUE;
Fail:
  VecFree (Out);
  return FALSE;
}

CONST PE_SECTION *
PeFind (
  IN CONST PE_SECTION *Sections,
  IN UINTN             Count,
  IN CONST CHAR8      *Name
)
{
  for (UINTN Index = 0; Index < Count; Index++) {
    if (AsciiStrEq (Sections[Index].Name, Name)) {
      return &Sections[Index];
    }
  }
  return NULL;
}
