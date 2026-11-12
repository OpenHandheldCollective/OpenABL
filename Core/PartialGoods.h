/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  PartialGoods - disables, in a mainline devicetree, the remote processors
  of the parts a chip has fused off.

  The ABL does this for Android in UpdatePartialGoodsNode, by node names
  that only Qualcomm's devicetrees use. Mainline names the nodes by address,
  so they are found here by their compatible.
*/

#ifndef __CORE_PARTIAL_GOODS_H__
#define __CORE_PARTIAL_GOODS_H__

#include "Base/OablBase.h"

// Bit numbers of ChipInfo's EFICHIPINFO_PART_* values.
#define PARTIAL_GOODS_MODEM     6
#define PARTIAL_GOODS_WLAN      7
#define PARTIAL_GOODS_COMP      8
#define PARTIAL_GOODS_SENSORS   9
#define PARTIAL_GOODS_COMPUTE_1 13
#define PARTIAL_GOODS_NSP       15

// Sets status = "disabled" on the enabled remote processors of the parts in
// Disabled, a mask of PARTIAL_GOODS_* bits. Fdt needs room to grow by a few
// bytes per node. Returns how many nodes were disabled, or a negative libfdt
// error.
INTN
PartialGoodsApply (
  IN OUT VOID *Fdt,
  IN UINT32    Disabled
);

#endif
