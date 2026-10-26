/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  KernelCmdLine - Kernel command line changes made by the ABL.
*/

#include "Core/KernelCmdLine.h"

#include "Base/Print.h"
#include "Base/Text.h"

// AYANEO/KONKR panels, as XBL names them.
STATIC CONST CHAR8 *CONST mOemPanels[] = {
  "mdss_dsi_panel_ar14_", "mdss_dsi_ar06_",   "mdss_dsi_ar07_",
  "mdss_dsi_kr04_",       "mdss_dsi_wt0630_",
};

STATIC VOID
Param (
  OUT KERNEL_CMDLINE_PARAM *Out,
  IN CONST CHAR8           *Key,
  IN UINT8                  Value
)
{
  Out->Key = Key;
  SPrint (Out->Value, sizeof (Out->Value), "0x%02x", Value);
}

UINTN
KernelCmdLineAndroidParams (
  IN CONST CHAR8          *PanelConfig,
  IN UINT32                Subtype,
  IN UINT8                 DisplayId,
  IN BOOLEAN               WithDisplayId,
  OUT KERNEL_CMDLINE_PARAM Out[KERNEL_CMDLINE_ANDROID_PARAMS]
)
{
  UINTN   Count = 0;
  BOOLEAN Oem   = FALSE;

  for (UINTN Index = 0; Index < sizeof (mOemPanels) / sizeof (mOemPanels[0]);
       Index++) {
    Oem |= TextContains (TextOf (PanelConfig), mOemPanels[Index]);
  }
  if (!Oem) {
    return 0;
  }
  Param (&Out[Count++], " androidboot.board_id=", (UINT8)Subtype);
  Param (&Out[Count++], " androidboot.project_id=", (UINT8)(Subtype >> 8));
  Param (&Out[Count++], " androidboot.client_id=", (UINT8)(Subtype >> 16));
  if (WithDisplayId) {
    Param (&Out[Count++], " androidboot.display_id=", DisplayId);
  }
  return Count;
}
