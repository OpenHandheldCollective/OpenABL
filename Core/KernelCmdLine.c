/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  KernelCmdLine - Kernel command line changes made by the ABL.
*/

#include "Core/KernelCmdLine.h"

#include "Base/Text.h"

// AYANEO Pocket S2/FIT panel, as named in XBL's DisplayPanelConfiguration.
#define POCKET_FIT_PANEL "mdss_dsi_panel_ar14_1080p_144hz_video"

BOOLEAN
KernelCmdLineFileText (
  IN CONST UINT8     *Raw,
  IN UINTN            Size,
  IN OUT OABL_BUFFER *Out
)
{
  if (!BufferReserve (Out, Size)) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Size; Index++) {
    Out->Data[Out->Len++] =
      Raw[Index] == '\n' || Raw[Index] == '\r' ? ' ' : Raw[Index];
  }
  while (Out->Len && Out->Data[Out->Len - 1] == ' ') {
    Out->Len--;
  }
  for (UINTN Index = 0; Index < Out->Len; Index++) {
    if (!Out->Data[Index]) {
      Out->Len = Index;
      break;
    }
  }
  return TRUE;
}

BOOLEAN
KernelCmdLineCombine (
  IN OABL_BYTES          Base,
  IN KERNEL_CMDLINE_KIND Kind,
  IN OABL_BYTES          Text,
  IN OUT OABL_BUFFER    *Out
)
{
  if (Kind == KERNEL_CMDLINE_KIND_APPEND) {
    if (!BufferAppend (Out, Base.Ptr, Base.Len)) {
      return FALSE;
    }
    if (Base.Len && Base.Ptr[Base.Len - 1] != ' ' &&
        !BufferAppendU8 (Out, ' ')) {
      return FALSE;
    }
  }
  return BufferAppend (Out, Text.Ptr, Text.Len);
}

UINTN
KernelCmdLineAndroidParams (
  IN CONST CHAR8          *PanelConfig,
  OUT KERNEL_CMDLINE_PARAM Out[KERNEL_CMDLINE_ANDROID_PARAMS]
)
{
  UINTN Count = 0;

  if (TextContains (TextOf (PanelConfig), POCKET_FIT_PANEL)) {
    Out[Count++] = (KERNEL_CMDLINE_PARAM){" androidboot.client_id=", "0x01"};
  }
  return Count;
}
