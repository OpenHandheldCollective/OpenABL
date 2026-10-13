/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Carveouts - per-SoC memory that the devicetrees keep from Linux.

  The tables list the no-map reserved regions. Every gap between them inside
  a DRAM bank is handed to the OS. They were generated from the ABL's
  SocCarveoutTable.h, the intersection of the Linux DTBs for each SoC.
  Efi/Handover.c applies the reclaim to the GCD map.
*/

#ifndef __CORE_CARVEOUTS_H__
#define __CORE_CARVEOUTS_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"

typedef struct _CARVEOUTS_REGION {
  UINT64 Base, Len;
} CARVEOUTS_REGION;

// No-map regions, sorted by base.
typedef struct _CARVEOUTS_TABLE {
  CONST CARVEOUTS_REGION *Regions;
  UINTN                   Count;
} CARVEOUTS_TABLE;

// The SM4450 DTBs (Anbernic RG 55G1, Retroid Pocket Classic and
// Classic Rev Q) all agree.
extern CONST CARVEOUTS_TABLE gCarveoutsSm4450;
// The TrimUI TG4070 DTB plus the stock hypervisor region: hypvm.mbn
// loads at 0x80000000, which XBL's memory map leaves out but the DTB
// doesn't reserve.
extern CONST CARVEOUTS_TABLE gCarveoutsSm6450;
extern CONST CARVEOUTS_TABLE gCarveoutsSm8250;
extern CONST CARVEOUTS_TABLE gCarveoutsSm8550;
extern CONST CARVEOUTS_TABLE gCarveoutsSm8650;
extern CONST CARVEOUTS_TABLE gCarveoutsSm8750;
extern CONST CARVEOUTS_TABLE gCarveoutsSm6115;
// From mainline sdm845.dtsi (Linux 7.2), as there is no SDM845 device tree. XBL
// leaves these out of its memory map, so the bank reclaim alone would give
// hypervisor, TZ and remote processor memory to the OS.
extern CONST CARVEOUTS_TABLE gCarveoutsSdm845;

// Returns the table for a ChipInfo chip id, or by Name if the id isn't
// known. NULL means reclaim from the banks alone.
CONST CARVEOUTS_TABLE *
CarveoutsForChip (
  IN UINT32       ChipId,
  IN CONST CHAR8 *Name OPTIONAL
);

typedef enum {
  CARVEOUTS_GCD_NON_EXISTENT,
  CARVEOUTS_GCD_RESERVED,
  // System memory, MMIO and the rest are left alone.
  CARVEOUTS_GCD_OTHER,
} CARVEOUTS_GCD_KIND;

typedef struct _CARVEOUTS_GCD {
  UINT64             Base, Len;
  CARVEOUTS_GCD_KIND Kind;
  // Capabilities include write-back.
  BOOLEAN WriteBack;
} CARVEOUTS_GCD;

// DRAM to hand to the OS, [Start, End). Reserved spans must be removed from
// the GCD before they are added back as system memory.
typedef struct _CARVEOUTS_SPAN {
  UINT64  Start, End;
  BOOLEAN Reserved;
} CARVEOUTS_SPAN;

typedef struct _CARVEOUTS_SPANS OABL_VEC (CARVEOUTS_SPAN) CARVEOUTS_SPANS;

// Finds the DRAM to hand over using a table: GCD holes and reserved spans
// in the gaps between no-map regions. Nothing overlapping Display, XBL's
// splash framebuffer, is taken.
BOOLEAN
CarveoutsSpansFromTable (
  IN CONST CARVEOUTS_REGION *Banks,
  IN UINTN                   BankCount,
  IN CONST CARVEOUTS_REGION *Nomap,
  IN UINTN                   NomapCount,
  IN CONST CARVEOUTS_GCD    *Gcd,
  IN UINTN                   GcdCount,
  IN CONST CARVEOUTS_REGION *Display OPTIONAL,
  OUT CARVEOUTS_SPANS       *Out
);

// Finds the DRAM to hand over without a table: GCD holes and write-back
// capable reserved spans, except those on Display.
BOOLEAN
CarveoutsSpansFromBanks (
  IN CONST CARVEOUTS_REGION *Banks,
  IN UINTN                   BankCount,
  IN CONST CARVEOUTS_GCD    *Gcd,
  IN UINTN                   GcdCount,
  IN CONST CARVEOUTS_REGION *Display OPTIONAL,
  OUT CARVEOUTS_SPANS       *Out
);

#endif
