/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Pe - Minimal PE/COFF parsing for Unified Kernel Image metadata.

  Reads sections such as .osrel and .cmdline without loading the image,
  and tells an EFI-stub kernel from a compressed one.
*/

#ifndef __CORE_PE_H__
#define __CORE_PE_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"

// IMAGE_FILE_MACHINE_ARM64.
#define PE_MACHINE_AARCH64 0xAA64

typedef struct _PE_SECTION {
  // Up to 8 Latin-1 characters, stored as UTF-8.
  CHAR8 Name[17];
  // The real data size.
  UINT32 VirtualSize;
  UINT32 FileOffset;
  // Rounded up to the file alignment.
  UINT32 FileSize;
} PE_SECTION;

typedef struct _PE_SECTIONS OABL_VEC (PE_SECTION) PE_SECTIONS;

// Returns the file offset and length of the section's data.
VOID
PeSectionDataRange (
  IN CONST PE_SECTION *Section,
  OUT UINT64          *Offset,
  OUT UINTN           *Length
);

BOOLEAN
PeIsPe (
  IN CONST UINT8 *Head,
  IN UINTN        Size
);

BOOLEAN
PeMachine (
  IN CONST UINT8 *Head,
  IN UINTN        Size,
  OUT UINT16     *Machine
);

// Reads the section table. Head must cover the headers; 4 KiB is enough.
BOOLEAN
PeSections (
  IN CONST UINT8  *Head,
  IN UINTN         Size,
  OUT PE_SECTIONS *Out
);

CONST PE_SECTION *
PeFind (
  IN CONST PE_SECTION *Sections,
  IN UINTN             Count,
  IN CONST CHAR8      *Name
);

#endif
