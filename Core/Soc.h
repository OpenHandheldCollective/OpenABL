/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Soc - the Qualcomm SoCs OpenABL knows, by ChipInfo id (GetChipId): the
  name the info card shows, and the root compatibles of their Linux
  devicetrees. Ids are from Linux's dt-bindings/arm/qcom,ids.h where it
  has them.
*/

#ifndef __CORE_SOC_H__
#define __CORE_SOC_H__

#include "Base/OablBase.h"

#include "Base/Text.h"

#define SOC_COMPATIBLES_MAX 2

typedef struct _SOC_INFO {
  UINT32 Id;
  // Marketing name, or NULL to show ChipInfo's own name.
  CONST CHAR8 *Name;
  // Strings any one of which a devicetree's root compatible lists for this
  // SoC; unused ones are NULL. All NULL if not known.
  CONST CHAR8 *Compatibles[SOC_COMPATIBLES_MAX];
} SOC_INFO;

// The SoC with ChipInfo id Id, or NULL.
CONST SOC_INFO *
SocById (IN UINT32 Id);

// Whether Soc has any compatibles to match devicetrees by.
BOOLEAN
SocHasCompatibles (IN CONST SOC_INFO *Soc);

// Whether List, a devicetree compatible property (NUL-terminated strings),
// has one of Soc's compatibles.
BOOLEAN
SocMatches (
  IN CONST SOC_INFO *Soc,
  IN OABL_BYTES      List
);

#endif
