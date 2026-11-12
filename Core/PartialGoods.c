/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

#include "Core/PartialGoods.h"

#include "Base/Text.h"
#include "Core/LibFdt.h"

typedef struct _PARTIAL_GOODS_RPROC {
  UINT32       Part;
  CONST CHAR8 *Suffix;
} PARTIAL_GOODS_RPROC;

// The ABL's PartialGoodsMmType entries for remote processors, by the end of
// their mainline compatible ("qcom,sm8750-cdsp-pas").
STATIC CONST PARTIAL_GOODS_RPROC mRprocs[] = {
  {PARTIAL_GOODS_COMP,      "-cdsp-pas" },
  {PARTIAL_GOODS_NSP,       "-cdsp-pas" },
  {PARTIAL_GOODS_COMPUTE_1, "-cdsp1-pas"},
  {PARTIAL_GOODS_MODEM,     "-mpss-pas" },
  {PARTIAL_GOODS_WLAN,      "-wpss-pas" },
  {PARTIAL_GOODS_SENSORS,   "-slpi-pas" },
};

STATIC BOOLEAN
IsDisabledPart (
  IN CONST CHAR8 *Compatible,
  IN INTN         Len,
  IN UINT32       Disabled
)
{
  for (INTN At = 0; At < Len; At += AsciiStrLen (Compatible + At) + 1) {
    for (UINTN Index = 0; Index < sizeof (mRprocs) / sizeof (mRprocs[0]);
         Index++) {
      if ((Disabled & (1u << mRprocs[Index].Part)) &&
          TextEndsWith (TextOf (Compatible + At), mRprocs[Index].Suffix)) {
        return TRUE;
      }
    }
  }
  return FALSE;
}

STATIC BOOLEAN
IsEnabled (
  IN CONST VOID *Fdt,
  IN INT32       Node
)
{
  CONST CHAR8 *Status = fdt_getprop (Fdt, Node, "status", NULL);

  return !Status || AsciiStrEq (Status, "okay") || AsciiStrEq (Status, "ok");
}

INTN
PartialGoodsApply (
  IN OUT VOID *Fdt,
  IN UINT32    Disabled
)
{
  STATIC CONST CHAR8 Status[] = "disabled";
  INTN               Count    = 0;

  for (INT32 Node = fdt_next_node (Fdt, -1, NULL); Node >= 0;
       Node       = fdt_next_node (Fdt, Node, NULL)) {
    INT32        Len;
    CONST CHAR8 *Compatible = fdt_getprop (Fdt, Node, "compatible", &Len);
    INT32        Error;

    if (!Compatible || !IsDisabledPart (Compatible, Len, Disabled) ||
        !IsEnabled (Fdt, Node)) {
      continue;
    }
    Error = fdt_setprop (Fdt, Node, "status", Status, sizeof (Status));
    if (Error < 0) {
      return Error;
    }
    Count++;
  }
  return Count;
}
