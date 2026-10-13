/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DeviceTree - devicetree inspection and in-place editing.

  DtInspect walks the blob itself with bounds-checked reads, since blobs
  come off SD cards. The rest uses the ABL's libfdt (Core/LibFdt.h), which
  trusts its input, so every call validates the blob first and leaves a bad
  one untouched: edits return DT_BAD_BLOB and lookups find nothing.

  Nodes are libfdt offsets. Edits grow the blob within its totalsize, which
  must fit in Capacity, and set a header version above 17 to 17. Paths must
  be absolute, as libfdt reads one without the leading '/' as an alias.

  Trees not built by dtc follow libfdt's assumptions: a node's properties
  come before its subnodes, edits need the memory reservation map before the
  structure block, and a last_comp_version above 17 is refused.
*/

#ifndef __CORE_DEVICE_TREE_H__
#define __CORE_DEVICE_TREE_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Base/Text.h"

typedef struct _DT_INFO {
  CHAR8 *Model;
  // First compatible string of the root, or NULL.
  CHAR8 *Compatible;
  // Rotation of the first panel* node, if it is a multiple of 90 degrees.
  BOOLEAN HasPanelRotation;
  UINT16  PanelRotation;
} DT_INFO;

// Fails if the blob isn't a valid FDT or has no model. Out may be NULL to
// only check that. This is a minimal walk, not a general parser, to keep the
// menu the ABL embeds small.
BOOLEAN
DtInspect (
  IN CONST UINT8 *Blob,
  IN UINTN        Size,
  OUT DT_INFO    *Out OPTIONAL
);

VOID
DtInfoFree (IN OUT DT_INFO *Info);

// libfdt's -FDT_ERR_NOSPACE becomes DT_NO_SPACE, other errors DT_BAD_BLOB.
typedef enum {
  DT_OK,
  // Can't be edited, or the subnode exists already (-FDT_ERR_EXISTS).
  DT_BAD_BLOB,
  DT_NOT_FOUND,
  // The property doesn't fit within the blob's totalsize, or out of memory.
  DT_NO_SPACE,
} DT_ERROR;

// Sets property Name on the first node compatible with one of Compatibles,
// tried in order. Also returns DT_NOT_FOUND if the header can't be read.
DT_ERROR
DtSetPropByCompatible (
  IN OUT UINT8          *Fdt,
  IN UINTN               Capacity,
  IN CONST CHAR8 *CONST *Compatibles,
  IN UINTN               Count,
  IN CONST CHAR8        *Name,
  IN CONST VOID         *Value,
  IN UINTN               Length
);

// Like DtSetPropByCompatible, on the node at offset Node.
DT_ERROR
DtSetPropAt (
  IN OUT UINT8   *Fdt,
  IN UINTN        Capacity,
  IN UINTN        Node,
  IN CONST CHAR8 *Name,
  IN CONST VOID  *Value,
  IN UINTN        Length
);

// Finds the node at Path with fdt_path_offset. A component without a unit
// address matches a node with one: /reserved-memory/cont_splash_region finds
// cont_splash_region@9c000000.
BOOLEAN
DtNodeAtPath (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN CONST CHAR8 *Path,
  OUT UINTN      *Node
);

// Name points into the blob.
BOOLEAN
DtNodeName (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        Node,
  OUT OABL_BYTES *Name
);

// Value points into the blob.
BOOLEAN
DtPropAt (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        Node,
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
);

typedef struct _DT_NODES
OABL_VEC (UINTN) DT_NODES;

// Finds every node with a compatible string that Want accepts. On failure,
// Found keeps the nodes found so far.
BOOLEAN
DtNodesWhere (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN BOOLEAN (*Want) (
    IN CONST UINT8 *Compatible,
    IN UINTN        Length,
    IN VOID        *Context
  ),
  IN VOID      *Context,
  OUT DT_NODES *Found
);

// Adds an empty subnode after Parent's properties and before its subnodes.
// Returns DT_BAD_BLOB if Parent already has a subnode of that name.
DT_ERROR
DtAddSubnode (
  IN OUT UINT8   *Fdt,
  IN UINTN        Capacity,
  IN UINTN        Parent,
  IN CONST CHAR8 *Name,
  OUT UINTN      *Node
);

// Returns TRUE if /chosen has a subnode whose name starts with Prefix. It
// doesn't allocate, so it can be called at ExitBootServices.
BOOLEAN
DtChosenHas (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN CONST CHAR8 *Prefix
);

// Lists the direct subnodes of Node. On failure, Out keeps those found so
// far.
BOOLEAN
DtNodeChildren (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        Node,
  OUT DT_NODES   *Out
);

// Adds a no-map framebuffer@BASE node to /reserved-memory, creating it if
// needed, unless a region there already covers the range. This keeps the
// kernel off the scanned-out memory when it takes its memory layout from the
// devicetree. Under EFI the firmware memory map already reserves it.
DT_ERROR
DtReserveMemory (
  IN OUT UINT8 *Fdt,
  IN UINTN      Capacity,
  IN UINT64     Base,
  IN UINT64     Size,
  OUT BOOLEAN  *Added
);

// Like DtReserveMemory, with the node named Prefix@BASE.
DT_ERROR
DtReserveMemoryNamed (
  IN OUT UINT8   *Fdt,
  IN UINTN        Capacity,
  IN CONST CHAR8 *Prefix,
  IN UINT64       Base,
  IN UINT64       Size,
  OUT BOOLEAN    *Added
);

typedef struct _DT_FRAMEBUFFER {
  UINT64 Base;
  UINT64 Size;
  UINT32 Width;
  UINT32 Height;
  // Bytes per line.
  UINT32 Stride;
  // simple-framebuffer pixel format (a8r8g8b8, ...).
  CONST CHAR8 *Format;
} DT_FRAMEBUFFER;

// Returns TRUE for the display nodes whose clocks the splash needs: MDSS,
// DPU, DSI controllers and DSI PHYs (qcom,sm8250-mdss, qcom,dsi-phy-7nm, ...).
BOOLEAN
DtIsDisplayNode (
  IN CONST UINT8 *Compatible,
  IN UINTN        Length
);

// What DtAddFramebuffer put in the node.
typedef struct _DT_FRAMEBUFFER_NODE {
  // Number of clock references copied.
  UINTN Clocks;
  // TRUE if the MDSS power-domains was copied.
  BOOLEAN PowerDomain;
} DT_FRAMEBUFFER_NODE;

// Adds a simple-framebuffer node, /chosen/framebuffer@BASE, so simpledrm
// keeps the splash on screen until the display driver takes over. The node
// gets the clocks of the enabled display nodes and the MDSS power-domains,
// so the kernel's unused-clock cleanup leaves them alone. If the node exists
// already, as in Retroid's SM8250 trees, the properties go on it.
DT_ERROR
DtAddFramebuffer (
  IN OUT UINT8            *Fdt,
  IN UINTN                 Capacity,
  IN CONST DT_FRAMEBUFFER *Framebuffer,
  OUT DT_FRAMEBUFFER_NODE *Out
);

#endif
