/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Dxe - GCD memory space calls from the DXE services table (PI spec, vol. 2).

  The memory reclaim uses these to add DRAM the firmware left out, and the
  XBL quirks use them to make a page of driver code writable. Every call
  fails if the firmware has no DXE services table.
*/

#ifndef __EFI_DXE_H__
#define __EFI_DXE_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Pi/PiDxeCis.h>

#include "Base/Buffer.h"
#include "Core/Carveouts.h"

// EfiGcdMemoryTypeSystemMemory, as DxeKindAt reports it.
#define DXE_GCD_SYSTEM_MEMORY 2u
_Static_assert (
  DXE_GCD_SYSTEM_MEMORY == EfiGcdMemoryTypeSystemMemory,
  "the PI spec's GCD type"
);

// All caching modes, as the ABL gives memory it adds.
#define DXE_ALL_CACHING                                                        \
  (EFI_MEMORY_UC | EFI_MEMORY_WC | EFI_MEMORY_WT | EFI_MEMORY_WB)

// The firmware's memory-space map is an array of the PI spec's 56-byte
// descriptors.
_Static_assert (
  sizeof (EFI_GCD_MEMORY_SPACE_DESCRIPTOR) == 56,
  "the PI spec's GCD descriptor"
);

typedef struct _DXE_GCD_MAP OABL_VEC (CARVEOUTS_GCD) DXE_GCD_MAP;

// Gets the GCD memory-space map in the reclaim's terms: NonExistent and
// Reserved keep their type, the rest is Other, and WriteBack comes from the
// capabilities. Caller frees Out with VecFree.
BOOLEAN
DxeMemorySpaceMap (OUT DXE_GCD_MAP *Out);

// Gets the GCD descriptor of the space holding Address.
BOOLEAN
DxeDescriptor (
  IN UINT64                            Address,
  OUT EFI_GCD_MEMORY_SPACE_DESCRIPTOR *Out
);

// Gets the GCD type of the space holding Address.
BOOLEAN
DxeKindAt (
  IN UINT64   Address,
  OUT UINT32 *Kind
);

EFI_STATUS
DxeSetAttributes (
  IN UINT64 Base,
  IN UINT64 Length,
  IN UINT64 Attributes
);

// Adds system memory with DXE_ALL_CACHING capabilities.
EFI_STATUS
DxeAddSystemMemory (
  IN UINT64 Base,
  IN UINT64 Length
);

EFI_STATUS
DxeRemove (
  IN UINT64 Base,
  IN UINT64 Length
);

#endif
