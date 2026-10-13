/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  PageTable - AArch64 stage-1 translation table walker, 4 KiB granule.

  Used by ExecMemory in Efi/Handover.c. Descriptors are accessed in place
  through XBL's identity map. The caller reads the registers and does the
  TLB maintenance.
*/

#ifndef __CORE_PAGE_TABLE_H__
#define __CORE_PAGE_TABLE_H__

#include "Base/OablBase.h"

// Leaf execute-never bits: UXN (54) and PXN (53) at EL1, XN (54) at EL2.
#define PAGE_TABLE_XN ((1ull << 54) | (1ull << 53))
// Table descriptor bits PXNTable (59) and UXNTable or XNTable (60).
#define PAGE_TABLE_XN_TABLE ((1ull << 60) | (1ull << 59))
// Output address field of a descriptor.
#define PAGE_TABLE_OA 0x0000fffffffff000ull

typedef struct _PAGE_TABLE {
  // Address of the table at StartLevel.
  UINT64 Root;
  UINT32 StartLevel;
  // TRUE for EL2's tables (TTBR0_EL2, TCR_EL2).
  BOOLEAN El2;
} PAGE_TABLE;

// Describes the tables given by TTBR0 and TCR. Fails unless the granule is
// 4 KiB. The start level follows from T0SZ.
BOOLEAN
PageTableFrom (
  IN UINT64       Ttbr,
  IN UINT64       Tcr,
  IN BOOLEAN      El2,
  OUT PAGE_TABLE *Out
);

// Returns the size mapped by one descriptor at Level.
UINT64
PageTableSpan (IN UINT32 Level);

// Returns the address of Va's leaf descriptor, or 0 if Va isn't mapped.
// With Clear, PAGE_TABLE_XN_TABLE is cleared in the tables on the way down.
UINT64
PageTableWalk (
  IN CONST PAGE_TABLE *Table,
  IN UINT64            Va,
  IN BOOLEAN           Clear,
  OUT UINT32          *Level
);

// Tells whether Va is mapped execute-never. Fails if Va isn't mapped.
BOOLEAN
PageTableIsXn (
  IN CONST PAGE_TABLE *Table,
  IN UINT64            Va,
  OUT BOOLEAN         *Xn
);

// Clears execute-never on [Start, End) and returns the leaves changed. A
// block partly in range is changed whole. The caller invalidates the TLB.
UINT64
PageTableClearXn (
  IN CONST PAGE_TABLE *Table,
  IN UINT64            Start,
  IN UINT64            End
);

#endif
