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

// Where a devicetree's blocks are, from its header.
typedef struct _DT_LAYOUT {
  // totalsize: the tree's size, from its start.
  UINTN Total;
  UINTN StructOffset;
  // The strings block runs from here to the end of the tree.
  UINTN StringsOffset;
} DT_LAYOUT;

// Reads the header of the tree that starts at Head. Fails if it isn't a
// devicetree header or its blocks don't fit in totalsize. Head may be just
// the start of the tree.
BOOLEAN
DtLayoutOf (
  IN CONST UINT8 *Head,
  IN UINTN        Len,
  OUT DT_LAYOUT  *Layout
);

typedef enum {
  DT_PEEK_FOUND,
  DT_PEEK_ABSENT,
  // Head ends before the root's properties do, or they don't parse.
  DT_PEEK_UNKNOWN,
} DT_PEEK;

// Finds the root's first property called Name with only part of the tree
// read: Head, its start, and Strings, from Layout's StringsOffset to its
// end, so a .dtb file's model and compatible can be read without reading
// the whole file. Value points into Head.
DT_PEEK
DtPeekRootProp (
  IN CONST UINT8     *Head,
  IN UINTN            HeadLen,
  IN CONST DT_LAYOUT *Layout,
  IN CONST UINT8     *Strings,
  IN UINTN            StringsLen,
  IN CONST CHAR8     *Name,
  OUT OABL_BYTES     *Value
);

// Where a devicetree is in a region.
typedef struct _DT_BLOB {
  UINTN Offset, Size;
} DT_BLOB;

typedef struct _DT_BLOBS OABL_VEC (DT_BLOB) DT_BLOBS;

// Finds every devicetree with a model in Region, such as a .dtb file with
// several concatenated. On failure, Found keeps the blobs found so far.
BOOLEAN
DtFindAll (
  IN CONST UINT8 *Region,
  IN UINTN        Size,
  OUT DT_BLOBS   *Found
);

// libfdt's -FDT_ERR_NOSPACE becomes DT_NO_SPACE, other errors DT_BAD_BLOB.
typedef enum {
  DT_OK,
  // Can't be edited, or the subnode exists already (-FDT_ERR_EXISTS).
  DT_BAD_BLOB,
  DT_NOT_FOUND,
  // The property doesn't fit within the blob's totalsize, or out of memory.
  DT_NO_SPACE,
} DT_ERROR;

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

// Adds a no-map Prefix@BASE node to /reserved-memory, creating it if
// needed, unless a region there already covers the range. This keeps the
// kernel off the scanned-out memory when it takes its memory layout from the
// devicetree. Under EFI the firmware memory map already reserves it.
DT_ERROR
DtReserveMemoryNamed (
  IN OUT UINT8   *Fdt,
  IN UINTN        Capacity,
  IN CONST CHAR8 *Prefix,
  IN UINT64       Base,
  IN UINT64       Size,
  OUT BOOLEAN    *Added
);

#endif
