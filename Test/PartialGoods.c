/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  PartialGoods - host test for PartialGoodsApply.

    partialgoods-test

  Fuses off the NSP and the modem in a built tree: the enabled compute DSP
  is disabled, the modem already is, and the audio DSP is left alone.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

#include "Core/PartialGoods.h"

#define CHECK(Cond)                                                            \
  do {                                                                         \
    if (!(Cond)) {                                                             \
      fprintf (stderr, "%s:%d: %s\n", __FILE__, __LINE__, #Cond);              \
      exit (1);                                                                \
    }                                                                          \
  } while (0)

STATIC VOID
Rproc (
  IN VOID        *Fdt,
  IN CONST CHAR8 *Name,
  IN CONST CHAR8 *Compatible,
  IN UINTN        CompatibleLen,
  IN CONST CHAR8 *Status OPTIONAL
)
{
  CHECK (fdt_begin_node (Fdt, Name) == 0);
  CHECK (fdt_property (Fdt, "compatible", Compatible, CompatibleLen) == 0);
  if (Status) {
    CHECK (fdt_property_string (Fdt, "status", Status) == 0);
  }
  CHECK (fdt_end_node (Fdt) == 0);
}

STATIC CONST CHAR8 *
StatusOf (
  IN CONST VOID  *Fdt,
  IN CONST CHAR8 *Path
)
{
  INT32 Node = fdt_path_offset (Fdt, Path);

  CHECK (Node >= 0);
  return fdt_getprop (Fdt, Node, "status", NULL);
}

int
main (VOID)
{
  STATIC CONST CHAR8 Cdsp[] = "qcom,sm8750-cdsp-pas";
  STATIC CONST CHAR8 Mpss[] = "qcom,sm8750-mpss-pas";
  STATIC CONST CHAR8 Adsp[] = "qcom,sm8750-adsp-pas";
  // A fallback compatible after the SoC's own.
  STATIC CONST CHAR8 Cdsp1[] = "qcom,x-cdsp1-pas\0qcom,y-cdsp1-pas";
  STATIC UINT8       Fdt[4096];
  CONST CHAR8       *Status;

  CHECK (fdt_create (Fdt, sizeof (Fdt)) == 0);
  CHECK (fdt_finish_reservemap (Fdt) == 0);
  CHECK (fdt_begin_node (Fdt, "") == 0);
  CHECK (fdt_begin_node (Fdt, "soc@0") == 0);
  Rproc (Fdt, "remoteproc@32300000", Cdsp, sizeof (Cdsp), "okay");
  Rproc (Fdt, "remoteproc@4080000", Mpss, sizeof (Mpss), "disabled");
  Rproc (Fdt, "remoteproc@6800000", Adsp, sizeof (Adsp), NULL);
  Rproc (Fdt, "remoteproc@40000000", Cdsp1, sizeof (Cdsp1), NULL);
  CHECK (fdt_end_node (Fdt) == 0);
  CHECK (fdt_end_node (Fdt) == 0);
  CHECK (fdt_finish (Fdt) == 0);
  CHECK (fdt_open_into (Fdt, Fdt, sizeof (Fdt)) == 0);

  CHECK (
    PartialGoodsApply (
      Fdt, 1u << PARTIAL_GOODS_NSP | 1u << PARTIAL_GOODS_MODEM |
             1u << PARTIAL_GOODS_COMPUTE_1
    ) == 2
  );
  Status = StatusOf (Fdt, "/soc@0/remoteproc@32300000");
  CHECK (Status && strcmp (Status, "disabled") == 0);
  Status = StatusOf (Fdt, "/soc@0/remoteproc@4080000");
  CHECK (Status && strcmp (Status, "disabled") == 0);
  CHECK (!StatusOf (Fdt, "/soc@0/remoteproc@6800000"));
  Status = StatusOf (Fdt, "/soc@0/remoteproc@40000000");
  CHECK (Status && strcmp (Status, "disabled") == 0);

  // Nothing fused off, nothing changed.
  CHECK (PartialGoodsApply (Fdt, 0) == 0);
  printf ("partial goods: ok\n");
  return 0;
}
