/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  PageTable - AArch64 stage-1 translation table walker, 4 KiB granule.
*/

#include "Core/PageTable.h"

STATIC UINT64
Read (IN UINT64 Slot)
{
  return *(volatile UINT64 *)(UINTN)Slot;
}

STATIC VOID
Write (
  IN UINT64 Slot,
  IN UINT64 Value
)
{
  *(volatile UINT64 *)(UINTN)Slot = Value;
}

BOOLEAN
PageTableFrom (
  IN UINT64       Ttbr,
  IN UINT64       Tcr,
  IN BOOLEAN      El2,
  OUT PAGE_TABLE *Out
)
{
  UINT64 Bits;

  // TG0 = 0 selects the 4 KiB granule.
  if (((Tcr >> 14) & 3) != 0) {
    return FALSE;
  }
  Bits            = 64 - (Tcr & 0x3f);
  Out->Root       = Ttbr & PAGE_TABLE_OA;
  Out->StartLevel = Bits > 39 ? 0 : Bits > 30 ? 1 : 2;
  Out->El2        = El2;
  return TRUE;
}

UINT64
PageTableSpan (IN UINT32 Level)
{
  return 1ull << (39 - 9 * Level);
}

UINT64
PageTableWalk (
  IN CONST PAGE_TABLE *Table,
  IN UINT64            Va,
  IN BOOLEAN           Clear,
  OUT UINT32          *Level
)
{
  UINT64 Base = Table->Root;

  *Level = Table->StartLevel;
  for (;;) {
    UINT64 Slot       = Base + ((Va >> (39 - 9 * *Level)) & 0x1ff) * 8;
    UINT64 Descriptor = Read (Slot);

    if ((Descriptor & 1) == 0) {
      return 0;
    }
    if (*Level == 3 || (Descriptor & 2) == 0) {
      return Slot;
    }
    if (Clear && (Descriptor & PAGE_TABLE_XN_TABLE) != 0) {
      Write (Slot, Descriptor & ~PAGE_TABLE_XN_TABLE);
    }
    Base = Descriptor & PAGE_TABLE_OA;
    (*Level)++;
  }
}

BOOLEAN
PageTableIsXn (
  IN CONST PAGE_TABLE *Table,
  IN UINT64            Va,
  OUT BOOLEAN         *Xn
)
{
  UINT32 Level;
  UINT64 Slot = PageTableWalk (Table, Va, FALSE, &Level);

  if (Slot == 0) {
    return FALSE;
  }
  *Xn = (Read (Slot) & PAGE_TABLE_XN) != 0;
  return TRUE;
}

UINT64
PageTableClearXn (
  IN CONST PAGE_TABLE *Table,
  IN UINT64            Start,
  IN UINT64            End
)
{
  UINT64 Changed = 0;
  UINT64 Va      = Start;

  while (Va < End) {
    UINT32 Level;
    UINT64 Slot = PageTableWalk (Table, Va, TRUE, &Level);
    UINT64 Next;

    if (Slot != 0) {
      UINT64 Descriptor = Read (Slot);

      if ((Descriptor & PAGE_TABLE_XN) != 0) {
        Write (Slot, Descriptor & ~PAGE_TABLE_XN);
        Changed++;
      }
    }
    Next = (Va | (PageTableSpan (Level) - 1)) + 1;
    if (Next <= Va) {
      break;
    }
    Va = Next;
  }
  return Changed;
}
