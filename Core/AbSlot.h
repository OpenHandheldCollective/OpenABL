/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  AbSlot - Qualcomm A/B slot state and the choice of slot to boot.

  The state lives in the GPT attribute bits of each slotted partition, in
  LinuxLoader's layout (PartitionTableUpdate.h):

    bits 48-49  priority (0-3)
    bit  50     active
    bits 51-53  retry count (0-7)
    bit  54     boot successful
    bit  55     unbootable
    bit  60     read-only (not slot related)

  Only boot_a and boot_b are consulted.
*/

#ifndef __CORE_AB_SLOT_H__
#define __CORE_AB_SLOT_H__

#include "Base/OablBase.h"

#define AB_PRIORITY_SHIFT 48
#define AB_ACTIVE_BIT     (1ull << 50)
#define AB_RETRY_SHIFT    51
#define AB_SUCCESSFUL_BIT (1ull << 54)
#define AB_UNBOOTABLE_BIT (1ull << 55)
#define AB_READ_ONLY_BIT  (1ull << 60)

typedef struct _AB_SLOT_STATE {
  UINT8   Priority;
  BOOLEAN Active;
  UINT8   Retry;
  BOOLEAN Successful;
  BOOLEAN Unbootable;
} AB_SLOT_STATE;

AB_SLOT_STATE
AbSlotStateFromAttributes (IN UINT64 Attributes);

// How the active slot was chosen.
typedef enum {
  // One bootable slot has the active bit.
  AB_REASON_MARKED,
  // Several are active. The highest priority wins, ties go to the lowest
  // letter.
  AB_REASON_HIGHEST_PRIORITY,
  // None is active, as on a freshly flashed device. LinuxLoader marks _a
  // active on first boot, so _a is reported.
  AB_REASON_DEFAULT_A,
} AB_REASON;

CONST CHAR8 *
AbReasonName (IN AB_REASON Reason);

typedef struct _AB_PARTITION {
  CONST CHAR8 *Name;
  UINT64       Attributes;
} AB_PARTITION;

// Chooses the slot to boot from the boot_a and boot_b attributes and returns
// its letter in Slot: the active, bootable slot with the highest priority,
// or slot a when no slot is marked active. Fails if there is no such slot.
BOOLEAN
AbActiveSlot (
  IN CONST AB_PARTITION *Partitions,
  IN UINTN               Count,
  OUT CHAR8             *Slot,
  OUT AB_REASON         *Reason
);

// Describes every slot on one line for the log. Caller frees the result.
CHAR8 *
AbDescribe (
  IN CONST AB_PARTITION *Partitions,
  IN UINTN               Count
);

#endif
