/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  AblImage - reads and checks a signed ABL image.
*/

#include "Core/AblImage.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Core/Sha2.h"

// Real images have three.
#define MAX_SEGMENTS 64

#define PT_NULL           0
#define SEGMENT_TYPE(F)   (((F) >> 24) & 7)
#define SEGMENT_TYPE_HASH 2

#define MBN_HASH_SHA384 3

typedef struct _SEGMENT {
  UINT32 Type;
  UINT32 Flags;
  UINT64 Offset;
  UINT64 FileSize;
} SEGMENT;

CONST CHAR8 *
AblImageResultName (IN ABL_IMAGE_RESULT Result)
{
  switch (Result) {
  case ABL_IMAGE_OK:
    return "ok";
  case ABL_IMAGE_NOT_ELF:
    return "not an ELF image";
  case ABL_IMAGE_NO_HASH_SEGMENT:
    return "no single hash segment";
  case ABL_IMAGE_BAD_HASH_SEGMENT:
    return "a hash segment XBL can't read";
  case ABL_IMAGE_BAD_DIGEST:
    return "a segment digest doesn't match";
  case ABL_IMAGE_BAD_TAG:
    return "a damaged image tag";
  }
  return "?";
}

STATIC BOOLEAN
ReadElf (
  IN CONST UINT8 *Data,
  IN UINTN        Size,
  OUT ABL_IMAGE  *Out,
  OUT SEGMENT     Segments[MAX_SEGMENTS],
  OUT UINTN      *Count
)
{
  BOOLEAN Is64;
  UINT64  PhOff;
  UINT16  PhEntSize;
  UINT16  PhNum;
  UINT64  End;

  if (Size < 52 || CompareMem (Data, "\177ELF", 4) != 0 || Data[5] != 1 ||
      (Data[4] != 1 && Data[4] != 2)) {
    return FALSE;
  }
  Is64         = Data[4] == 2;
  Out->Class   = Data[4];
  Out->Machine = ReadLe16 (Data + 18);
  if (Is64) {
    if (Size < 64) {
      return FALSE;
    }
    Out->Entry = ReadLe64 (Data + 24);
    PhOff      = ReadLe64 (Data + 32);
    PhEntSize  = ReadLe16 (Data + 54);
    PhNum      = ReadLe16 (Data + 56);
  } else {
    Out->Entry = ReadLe32 (Data + 24);
    PhOff      = ReadLe32 (Data + 28);
    PhEntSize  = ReadLe16 (Data + 42);
    PhNum      = ReadLe16 (Data + 44);
  }
  if (PhEntSize < (Is64 ? 56 : 32) || PhNum == 0 || PhNum > MAX_SEGMENTS ||
      PhOff > Size || (UINT64)PhEntSize * PhNum > Size - PhOff) {
    return FALSE;
  }
  End = PhOff + (UINT64)PhEntSize * PhNum;
  for (UINTN Index = 0; Index < PhNum; Index++) {
    CONST UINT8 *P = Data + PhOff + Index * PhEntSize;
    SEGMENT     *S = &Segments[Index];

    S->Type = ReadLe32 (P);
    if (Is64) {
      S->Flags    = ReadLe32 (P + 4);
      S->Offset   = ReadLe64 (P + 8);
      S->FileSize = ReadLe64 (P + 32);
    } else {
      S->Offset   = ReadLe32 (P + 4);
      S->FileSize = ReadLe32 (P + 16);
      S->Flags    = ReadLe32 (P + 24);
    }
    if (S->Offset > Size || S->FileSize > Size - S->Offset) {
      return FALSE;
    }
    if (S->Offset + S->FileSize > End) {
      End = S->Offset + S->FileSize;
    }
  }
  Out->Size = (UINTN)End;
  *Count    = PhNum;
  return TRUE;
}

// Finds the digest table in the hash segment.
STATIC BOOLEAN
ReadMbn (
  IN CONST UINT8 *Seg,
  IN UINT64       Length,
  IN UINTN        Count,
  OUT ABL_IMAGE  *Out,
  OUT UINT64     *TableAt
)
{
  UINT64 At;

  if (Length < 48) {
    return FALSE;
  }
  Out->MbnVersion = ReadLe32 (Seg + 4);
  switch (Out->MbnVersion) {
  case 3:
  case 5:
    Out->DigestSize = SHA256_SIZE;
    At              = 40;
    break;
  case 6:
    Out->DigestSize = SHA384_SIZE;
    At              = 48 + (UINT64)ReadLe32 (Seg + 40) + ReadLe32 (Seg + 44);
    break;
  case 7:
    if (ReadLe32 (Seg + 8) < 24 || Length < 64 ||
        ReadLe32 (Seg + 56) != MBN_HASH_SHA384) {
      return FALSE;
    }
    Out->DigestSize = SHA384_SIZE;
    Out->SoftwareId = ReadLe32 (Seg + 48);
    At              = 40 + (UINT64)ReadLe32 (Seg + 8) + ReadLe32 (Seg + 12) +
                      ReadLe32 (Seg + 16);
    break;
  default:
    return FALSE;
  }
  if (Out->MbnVersion == 7) {
    Out->QcomSignatureSize = ReadLe32 (Seg + 24);
    Out->QcomCertChainSize = ReadLe32 (Seg + 28);
    Out->SignatureSize     = ReadLe32 (Seg + 32);
  } else {
    if (Out->MbnVersion != 3) {
      Out->QcomSignatureSize = ReadLe32 (Seg + 8);
      Out->QcomCertChainSize = ReadLe32 (Seg + 12);
    }
    Out->SignatureSize = ReadLe32 (Seg + 28);
  }
  Out->CertChainSize = ReadLe32 (Seg + 36);
  if (ReadLe32 (Seg + 20) != Count * Out->DigestSize ||
      At + (UINT64)Count * Out->DigestSize > Length) {
    return FALSE;
  }
  *TableAt = At;
  return TRUE;
}

STATIC BOOLEAN
AllZero (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size
)
{
  for (UINTN Index = 0; Index < Size; Index++) {
    if (Bytes[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

// Printable ASCII, NUL padded, not empty.
STATIC BOOLEAN
TagText (
  IN CONST UINT8 *Field,
  OUT CHAR8       Out[ABL_IMAGE_TEXT_MAX + 1]
)
{
  UINTN Length = 0;

  while (Length < ABL_IMAGE_TEXT_MAX && Field[Length]) {
    if (Field[Length] < 0x21 || Field[Length] > 0x7e) {
      return FALSE;
    }
    Out[Length] = (CHAR8)Field[Length];
    Length++;
  }
  Out[Length] = '\0';
  return Length > 0 && AllZero (Field + Length, ABL_IMAGE_TEXT_MAX - Length);
}

// No tag is fine, two is not.
STATIC BOOLEAN
ReadTag (
  IN CONST UINT8 *Data,
  IN UINTN        Size,
  OUT ABL_IMAGE  *Out
)
{
  CONST UINT8 *Found = NULL;
  UINTN        Magic = sizeof (ABL_IMAGE_TAG_MAGIC) - 1;

  for (UINTN At = 0; At + Magic <= Size; At++) {
    if (Data[At] == ABL_IMAGE_TAG_MAGIC[0] &&
        CompareMem (Data + At, ABL_IMAGE_TAG_MAGIC, Magic) == 0) {
      if (Found || !InBounds (Size, At, ABL_IMAGE_TAG_SIZE)) {
        return FALSE;
      }
      Found = Data + At;
    }
  }
  return !Found || (TagText (Found + Magic, Out->Chipset) &&
                    TagText (Found + Magic + ABL_IMAGE_TEXT_MAX, Out->Version));
}

ABL_IMAGE_RESULT
AblImageRead (
  IN CONST UINT8 *Data,
  IN UINTN        Size,
  IN BOOLEAN      CheckDigests,
  OUT ABL_IMAGE  *Out,
  OUT UINT32     *BadSegment OPTIONAL
)
{
  SEGMENT      Segments[MAX_SEGMENTS];
  UINTN        Count;
  UINTN        Hash = MAX_UINTN;
  UINT64       TableAt;
  CONST UINT8 *Table;
  UINT8        Digest[SHA384_SIZE];

  ZeroMem (Out, sizeof (*Out));
  if (!ReadElf (Data, Size, Out, Segments, &Count)) {
    return ABL_IMAGE_NOT_ELF;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    if (Segments[Index].Type == PT_NULL &&
        SEGMENT_TYPE (Segments[Index].Flags) == SEGMENT_TYPE_HASH) {
      if (Hash != MAX_UINTN) {
        return ABL_IMAGE_NO_HASH_SEGMENT;
      }
      Hash = Index;
    }
  }
  if (Hash == MAX_UINTN) {
    return ABL_IMAGE_NO_HASH_SEGMENT;
  }
  if (!ReadMbn (
        Data + Segments[Hash].Offset, Segments[Hash].FileSize, Count, Out,
        &TableAt
      )) {
    return ABL_IMAGE_BAD_HASH_SEGMENT;
  }
  Table = Data + Segments[Hash].Offset + TableAt;
  for (UINTN Index = 0; CheckDigests && Index < Count; Index++) {
    CONST UINT8 *Want = Table + Index * Out->DigestSize;
    BOOLEAN      Good;

    if (Index == Hash || Segments[Index].FileSize == 0) {
      Good = AllZero (Want, Out->DigestSize);
    } else {
      if (Out->DigestSize == SHA256_SIZE) {
        Sha256 (
          Data + Segments[Index].Offset, (UINTN)Segments[Index].FileSize, Digest
        );
      } else {
        Sha384 (
          Data + Segments[Index].Offset, (UINTN)Segments[Index].FileSize, Digest
        );
      }
      Good = CompareMem (Want, Digest, Out->DigestSize) == 0;
    }
    if (!Good) {
      if (BadSegment) {
        *BadSegment = (UINT32)Index;
      }
      return ABL_IMAGE_BAD_DIGEST;
    }
  }
  return ReadTag (Data, Out->Size, Out) ? ABL_IMAGE_OK : ABL_IMAGE_BAD_TAG;
}

CONST CHAR8 *
AblImageMismatch (
  IN CONST ABL_IMAGE *Current,
  IN CONST ABL_IMAGE *New
)
{
  if (New->Class != Current->Class || New->Machine != Current->Machine) {
    return "ELF class or machine";
  }
  if (New->MbnVersion != Current->MbnVersion ||
      New->DigestSize != Current->DigestSize) {
    return "MBN version";
  }
  if (New->SoftwareId != Current->SoftwareId) {
    return "software id";
  }
  if ((New->QcomSignatureSize != 0) != (Current->QcomSignatureSize != 0) ||
      (New->QcomCertChainSize != 0) != (Current->QcomCertChainSize != 0)) {
    return "Qualcomm signature";
  }
  if (New->SignatureSize != Current->SignatureSize ||
      (New->CertChainSize != 0) != (Current->CertChainSize != 0)) {
    return "signature";
  }
  return NULL;
}
