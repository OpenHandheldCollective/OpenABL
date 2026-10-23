/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Battery - whether a charger is in and the battery's voltage.
*/

#include "Efi/Battery.h"

#include <Protocol/EFIChargerEx.h>

BOOLEAN
BatteryRead (OUT BATTERY_STATE *Out)
{
  EFI_CHARGER_EX_PROTOCOL *Charger = LocateProtocol (&gChargerExProtocolGuid);
  BOOLEAN                  Present = FALSE;
  UINT32                   Voltage = 0;

  *Out = (BATTERY_STATE){0};
  if (!Charger || Charger->GetChargerPresence (&Present) != EFI_SUCCESS) {
    return FALSE;
  }
  if (Charger->GetBatteryVoltage (&Voltage) != EFI_SUCCESS) {
    Voltage = 0;
  }
  *Out = (BATTERY_STATE){.Charger = Present, .MilliVolts = Voltage};
  return TRUE;
}
