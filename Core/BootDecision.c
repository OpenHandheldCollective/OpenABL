/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootDecision - boot mode and boot source decisions.
*/

#include "Core/BootDecision.h"

#include "Base/Buffer.h"

BOOLEAN
BootDecisionFastboot (IN CONST BOOT_DECISION_START *Start)
{
  return Start->Fastboot || Start->Key == BOOT_DECISION_SCAN_DELETE;
}

BOOLEAN
BootDecisionUpHeld (IN CONST BOOT_DECISION_START *Start)
{
  return !Start->Fastboot && (Start->Key == BOOT_DECISION_SCAN_UP ||
                              Start->Key == BOOT_DECISION_SCAN_HOME);
}

BOOLEAN
BootDecisionAndroid (
  IN CONST BOOT_DECISION_START *Start,
  IN BOOLEAN                    AndroidSetting,
  IN BOOLEAN                    Charger
)
{
  return AndroidSetting || BootDecisionUpHeld (Start) || Charger;
}

BOOLEAN
BootDecisionAltBoot (
  IN CONST BOOT_DECISION_START *Start,
  IN BOOLEAN                    AltSetting
)
{
  return AltSetting && BootDecisionUpHeld (Start);
}

BOOLEAN
BootDecisionRecovery (
  IN CONST BOOT_DECISION_START *Start,
  IN BOOLEAN                    AndroidSetting
)
{
  if (AndroidSetting) {
    return Start->Recovery || Start->Key == BOOT_DECISION_SCAN_HOME;
  }
  return Start->Recovery && Start->Key != BOOT_DECISION_SCAN_UP;
}

// Boot sources

// Messaging device path type and the subtypes used here.
#define MSG_USB       5
#define MSG_USB_CLASS 15
#define MSG_SD        26
#define MESSAGING     3

BOOT_SOURCE_MEDIUM
BootSourceMediumOf (IN BOOLEAN Removable)
{
  return (BOOT_SOURCE_MEDIUM){.External = Removable, .Usb = FALSE};
}

VOID
BootSourceMediumNode (
  IN OUT BOOT_SOURCE_MEDIUM *Medium,
  IN UINT8                   Type,
  IN UINT8                   SubType
)
{
  if (Type != MESSAGING) {
    return;
  }
  if (SubType == MSG_USB || SubType == MSG_USB_CLASS) {
    Medium->Usb      = TRUE;
    Medium->External = TRUE;
  } else if (SubType == MSG_SD) {
    Medium->External = TRUE;
  }
}

BOOLEAN
BootSourceMediumInternal (IN BOOT_SOURCE_MEDIUM Medium)
{
  return !Medium.External && !Medium.Usb;
}

BOOT_SOURCE
BootSourceEffective (
  IN BOOT_SOURCE Source,
  IN BOOLEAN     CfwOnInternal
)
{
  return Source == BOOT_SOURCE_INTERNAL && !CfwOnInternal ? BOOT_SOURCE_SD
                                                          : Source;
}

BOOLEAN
BootSourceAllows (
  IN BOOT_SOURCE        Source,
  IN BOOLEAN            AltVolumeBoot,
  IN BOOT_SOURCE_MEDIUM Medium
)
{
  if (AltVolumeBoot) {
    return Medium.External;
  }
  switch (Source) {
  case BOOT_SOURCE_USB:
    return Medium.Usb;
  case BOOT_SOURCE_SD:
    return Medium.External && !Medium.Usb;
  case BOOT_SOURCE_AUTO:
    return !Medium.Usb;
  case BOOT_SOURCE_INTERNAL:
    return BootSourceMediumInternal (Medium);
  }
  return FALSE;
}

// Sorts internal drives first. The elements are indices into Context, the
// media array.
STATIC BOOLEAN
InternalFirst (
  IN CONST VOID *A,
  IN CONST VOID *B,
  IN VOID       *Context
)
{
  CONST BOOT_SOURCE_MEDIUM *Media = Context;

  return BootSourceMediumInternal (Media[*(CONST UINTN *)A]) &&
         !BootSourceMediumInternal (Media[*(CONST UINTN *)B]);
}

UINTN
BootSourceSelect (
  IN BOOT_SOURCE               Source,
  IN BOOLEAN                   CfwOnInternal,
  IN BOOLEAN                   AltVolumeBoot,
  IN CONST BOOT_SOURCE_MEDIUM *Media,
  IN UINTN                     Count,
  OUT UINTN                   *Order
)
{
  UINTN Kept = 0;

  Source = BootSourceEffective (Source, CfwOnInternal);
  for (UINTN Index = 0; Index < Count; Index++) {
    Order[Index] = Index;
  }
  if (Source == BOOT_SOURCE_AUTO && !AltVolumeBoot) {
    SortBy (Order, Count, sizeof (*Order), InternalFirst, (VOID *)Media);
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    if (BootSourceAllows (Source, AltVolumeBoot, Media[Order[Index]])) {
      Order[Kept++] = Order[Index];
    }
  }
  return Kept;
}
