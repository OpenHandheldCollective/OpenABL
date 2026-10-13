/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  AbSlot - Qualcomm A/B slot state and the choice of slot to boot.
*/

#include "Core/AbSlot.h"

#include "Base/Text.h"

AB_SLOT_STATE
AbSlotStateFromAttributes (IN UINT64 Attributes)
{
  return (AB_SLOT_STATE){
    .Priority   = (UINT8)((Attributes >> AB_PRIORITY_SHIFT) & 0x3),
    .Active     = (Attributes & AB_ACTIVE_BIT) != 0,
    .Retry      = (UINT8)((Attributes >> AB_RETRY_SHIFT) & 0x7),
    .Successful = (Attributes & AB_SUCCESSFUL_BIT) != 0,
    .Unbootable = (Attributes & AB_UNBOOTABLE_BIT) != 0,
  };
}

CONST CHAR8 *
AbReasonName (IN AB_REASON Reason)
{
  switch (Reason) {
  case AB_REASON_MARKED:
    return "Marked";
  case AB_REASON_HIGHEST_PRIORITY:
    return "HighestPriority";
  case AB_REASON_DEFAULT_A:
    return "DefaultA";
  }
  return "?";
}

// Returns the slot letter of a partition named boot_a to boot_z.
STATIC BOOLEAN
SlotLetter (
  IN CONST CHAR8 *Name,
  OUT CHAR8      *Letter
)
{
  if (!TextStartsWith (TextOf (Name), "boot_") || Name[5] < 'a' ||
      Name[5] > 'z' || Name[6]) {
    return FALSE;
  }
  *Letter = Name[5];
  return TRUE;
}

BOOLEAN
AbActiveSlot (
  IN CONST AB_PARTITION *Partitions,
  IN UINTN               Count,
  OUT CHAR8             *Slot,
  OUT AB_REASON         *Reason
)
{
  UINTN   Slots        = 0;
  UINTN   Marked       = 0;
  BOOLEAN AnyActive    = FALSE;
  BOOLEAN ABootable    = FALSE;
  CHAR8   Best         = 0;
  UINT8   BestPriority = 0;

  // The result doesn't depend on the order of the partitions.
  for (UINTN Index = 0; Index < Count; Index++) {
    AB_SLOT_STATE State;
    CHAR8         Letter;

    if (!SlotLetter (Partitions[Index].Name, &Letter)) {
      continue;
    }
    State = AbSlotStateFromAttributes (Partitions[Index].Attributes);
    Slots++;
    AnyActive |= State.Active;
    if (Letter == 'a' && !State.Unbootable) {
      ABootable = TRUE;
    }
    if (!State.Active || State.Unbootable) {
      continue;
    }
    if (!Marked || State.Priority > BestPriority ||
        (State.Priority == BestPriority && Letter < Best)) {
      Best         = Letter;
      BestPriority = State.Priority;
    }
    Marked++;
  }
  if (!Slots) {
    return FALSE;
  }
  if (Marked == 1) {
    *Slot   = Best;
    *Reason = AB_REASON_MARKED;
    return TRUE;
  }
  if (Marked == 0) {
    if (AnyActive || !ABootable) {
      return FALSE;
    }
    *Slot   = 'a';
    *Reason = AB_REASON_DEFAULT_A;
    return TRUE;
  }
  *Slot   = Best;
  *Reason = AB_REASON_HIGHEST_PRIORITY;
  return TRUE;
}

CHAR8 *
AbDescribe (
  IN CONST AB_PARTITION *Partitions,
  IN UINTN               Count
)
{
  OABL_STRING Out = {0};

  for (UINTN Index = 0; Index < Count; Index++) {
    AB_SLOT_STATE State;

    if (!TextStartsWith (TextOf (Partitions[Index].Name), "boot_")) {
      continue;
    }
    State = AbSlotStateFromAttributes (Partitions[Index].Attributes);
    if (Out.Len) {
      StringAppend (&Out, ", ");
    }
    StringPrint (
      &Out, "_%s: prio %u retry %u%s%s%s", Partitions[Index].Name + 5,
      (UINT32)State.Priority, (UINT32)State.Retry,
      State.Active ? " active" : "", State.Successful ? " successful" : "",
      State.Unbootable ? " unbootable" : ""
    );
  }
  return StringTake (&Out);
}
