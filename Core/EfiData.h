/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  EfiData - UEFI boot manager data formats as plain bytes.

  EFI_LOAD_OPTION (UEFI 2.10, section 3.1.3), BootOrder, device path nodes
  and EFI_RT_PROPERTIES_TABLE (section 4.6.2). No UEFI calls or headers, so
  this can be tested on the host.

  Functions that fill a buffer or list start it empty and leave it empty on
  failure. Caller frees it.
*/

#ifndef __CORE_EFI_DATA_H__
#define __CORE_EFI_DATA_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Base/Text.h"

#define UEFI_GLOBAL_VARIABLE_GUID     "8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define UEFI_DTB_TABLE_GUID           "b1b621d5-f19c-41a5-830b-d9152c69aae0"
#define UEFI_RT_PROPERTIES_TABLE_GUID "eb66918a-7eef-402a-842e-931d21c38ae9"

#define UEFI_LOAD_OPTION_ACTIVE 0x00000001u

// Removable media default boot file for AArch64 (UEFI section 3.5.1.1).
#define UEFI_DEFAULT_BOOT_FILE "\\EFI\\BOOT\\BOOTAA64.EFI"

// Writes the Boot#### variable name. 9 bytes are enough.
VOID
UefiBootVarName (
  IN UINT16  Number,
  OUT CHAR8 *Out,
  IN UINTN   Size
);

typedef struct _UEFI_U16_LIST
OABL_VEC (UINT16) UEFI_U16_LIST;

// Decodes little-endian UINT16s. An odd last byte is ignored.
BOOLEAN
UefiParseU16List (
  IN CONST UINT8    *Bytes,
  IN UINTN           Size,
  OUT UEFI_U16_LIST *Out
);

BOOLEAN
UefiU16ListBytes (
  IN CONST UINT16 *Values,
  IN UINTN         Count,
  OUT OABL_BUFFER *Out
);

// EFI_LOAD_OPTION. Owns all its fields.
typedef struct _UEFI_LOAD_OPTION {
  UINT32 Attributes;
  CHAR8 *Description;
  // Device path list, end node included.
  OABL_BUFFER DevicePath;
  OABL_BUFFER OptionalData;
} UEFI_LOAD_OPTION;

// Fails on a truncated load option.
BOOLEAN
UefiLoadOptionParse (
  IN CONST UINT8       *Bytes,
  IN UINTN              Size,
  OUT UEFI_LOAD_OPTION *Out
);

BOOLEAN
UefiLoadOptionToBytes (
  IN CONST UEFI_LOAD_OPTION *Option,
  OUT OABL_BUFFER           *Out
);

VOID
UefiLoadOptionFree (IN OUT UEFI_LOAD_OPTION *Option);

STATIC inline BOOLEAN
UefiLoadOptionIsActive (IN CONST UEFI_LOAD_OPTION *Option)
{
  return (Option->Attributes & UEFI_LOAD_OPTION_ACTIVE) != 0;
}

// Device paths

#define UEFI_TYPE_HARDWARE      0x01
#define UEFI_TYPE_MEDIA         0x04
#define UEFI_TYPE_END           0x7F
#define UEFI_MEDIA_HARD_DRIVE   0x01
#define UEFI_MEDIA_FILE_PATH    0x04
#define UEFI_MEDIA_PIWG_FW_FILE 0x06
#define UEFI_MEDIA_PIWG_FW_VOL  0x07
#define UEFI_END_ENTIRE         0xFF

// A device path node, borrowed from a byte buffer.
typedef struct _UEFI_NODE {
  UINT8 Type, SubType;
  // The whole node, header included.
  OABL_BYTES Bytes;
} UEFI_NODE;

STATIC inline OABL_BYTES
UefiNodeData (IN CONST UEFI_NODE *Node)
{
  return (OABL_BYTES){Node->Bytes.Ptr + 4, Node->Bytes.Len - 4};
}

BOOLEAN
UefiNodeEq (
  IN CONST UEFI_NODE *A,
  IN CONST UEFI_NODE *B
);

// Steps through the nodes of the first instance of a path. Start with
// *At = 0. Fails at the end node or on malformed input.
BOOLEAN
UefiNextNode (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size,
  IN OUT UINTN   *At,
  OUT UEFI_NODE  *Out
);

// Returns TRUE if both paths have the same nodes, as UefiNextNode sees them.
BOOLEAN
UefiSameNodes (
  IN CONST UINT8 *A,
  IN UINTN        ASize,
  IN CONST UINT8 *B,
  IN UINTN        BSize
);

extern CONST UINT8 gUefiEndNode[4];

// Joins the nodes of Parts into one device path with an end node.
BOOLEAN
UefiJoin (
  IN CONST OABL_BYTES *Parts,
  IN UINTN             Count,
  OUT OABL_BUFFER     *Out
);

// Builds a media FilePath node for a backslash-separated Path.
BOOLEAN
UefiFilePathNode (
  IN CONST CHAR8  *Path,
  OUT OABL_BUFFER *Out
);

// Copies the GPT partition GUID of the path's last HD() node, as the GPT
// entry stores it. FALSE if there is none.
BOOLEAN
UefiPartGuid (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size,
  OUT UINT8       Guid[16]
);

// Size for UefiPartUuid's text: a GUID and its NUL.
#define UEFI_PART_UUID_SIZE 37

// Writes the GPT partition GUID of the path's last HD() node into Out, upper
// case as systemd's LoaderDevicePartUUID has it, or lower case as Linux's
// PARTUUID does. Returns FALSE, with Out empty, if there is none.
BOOLEAN
UefiPartUuid (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size,
  IN BOOLEAN      Upper,
  OUT CHAR8       Out[UEFI_PART_UUID_SIZE]
);

// Returns the joined FilePath text of a device path, or NULL if it has
// none. Caller frees the result.
CHAR8 *
UefiFilePathText (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size
);

// Returns TRUE if the nodes of Prefix match the start of DevicePath. An
// empty Prefix never matches.
BOOLEAN
UefiStartsWith (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size,
  IN CONST UINT8 *Prefix,
  IN UINTN        PrefixSize
);

// How a load option's device path must be resolved before LoadImage.
typedef enum {
  // Points into a firmware volume (setup menus, built-in shells). Not an OS
  // loader, so it is skipped.
  UEFI_PATH_FIRMWARE,
  // Starts with a hard drive (partition) node. Expand it by finding the
  // partition with the same signature.
  UEFI_PATH_SHORT_FORM_HARD_DRIVE,
  // Starts with a file path. Try it on every filesystem.
  UEFI_PATH_SHORT_FORM_FILE,
  // A full path to a file.
  UEFI_PATH_FULL_FILE,
  // A device without a file. Boot its default file (section 3.1.2).
  UEFI_PATH_DEVICE_ONLY,
} UEFI_PATH_KIND;

UEFI_PATH_KIND
UefiClassify (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size
);

// Name of the kind for the log.
CONST CHAR8 *
UefiPathKindName (IN UEFI_PATH_KIND Kind);

// Splits a path at the first FilePath node into a device part and a file
// part, each with an end node.
BOOLEAN
UefiSplitFile (
  IN CONST UINT8  *DevicePath,
  IN UINTN         Size,
  OUT OABL_BUFFER *Device,
  OUT OABL_BUFFER *File
);

// Offsets of HeaderSize and CRC32 in EFI_TABLE_HEADER (UEFI section 4.2).
#define UEFI_TABLE_HEADER_SIZE  12
#define UEFI_TABLE_HEADER_CRC32 16

// Recomputes a table's CRC32 after an entry changed, as the firmware does:
// zero the field, then take the CRC-32 of HeaderSize bytes.
VOID
UefiTableHeaderCrc (IN OUT UINT8 *Table);

// EFI_RT_PROPERTIES_TABLE

#define UEFI_RT_GET_TIME                      0x0001u
#define UEFI_RT_SET_TIME                      0x0002u
#define UEFI_RT_GET_WAKEUP_TIME               0x0004u
#define UEFI_RT_SET_WAKEUP_TIME               0x0008u
#define UEFI_RT_GET_VARIABLE                  0x0010u
#define UEFI_RT_GET_NEXT_VARIABLE_NAME        0x0020u
#define UEFI_RT_SET_VARIABLE                  0x0040u
#define UEFI_RT_SET_VIRTUAL_ADDRESS_MAP       0x0080u
#define UEFI_RT_CONVERT_POINTER               0x0100u
#define UEFI_RT_GET_NEXT_HIGH_MONOTONIC_COUNT 0x0200u
#define UEFI_RT_RESET_SYSTEM                  0x0400u
#define UEFI_RT_UPDATE_CAPSULE                0x0800u
#define UEFI_RT_QUERY_CAPSULE_CAPABILITIES    0x1000u
#define UEFI_RT_QUERY_VARIABLE_INFO           0x2000u
#define UEFI_RT_ALL                           0x3FFFu

// Writes { Version = 1, Length = 8, RuntimeServicesSupported }.
VOID
UefiRtPropertiesTable (
  IN UINT32 Supported,
  OUT UINT8 Out[8]
);

// EFI_SYSTEM_RESOURCE_TABLE (UEFI section 23.4)

#define UEFI_ESRT_FW_TYPE_SYSTEM 1
// The header and one 40-byte entry.
#define UEFI_ESRT_ONE_SIZE 56

// Writes an ESRT with one entry for FwClass (an EFI_GUID's 16 bytes): the
// firmware type and version, lowest supported version 0, no capsule flags,
// and a successful last attempt at Version.
VOID
UefiEsrtOne (
  IN CONST UINT8 FwClass[16],
  IN UINT32      Type,
  IN UINT32      Version,
  OUT UINT8      Out[UEFI_ESRT_ONE_SIZE]
);

// "1.2" as 0x00010002: the major number in the high 16 bits and the minor in
// the low, as fwupd's "pair" version format reads it. 0 if Version doesn't
// start with a number.
UINT32
UefiVersionPair (IN CONST CHAR8 *Version);

// Fails if the table is too short.
BOOLEAN
UefiParseRtProperties (
  IN CONST UINT8 *Table,
  IN UINTN        Size,
  OUT UINT16     *Version,
  OUT UINT32     *Supported
);

// Copies a DTB and grows its totalsize by Pad bytes of free space, so the
// next stage can add /chosen properties in place. Fails if Blob isn't a DTB.
BOOLEAN
UefiPaddedDtb (
  IN CONST UINT8  *Blob,
  IN UINTN         Size,
  IN UINTN         Pad,
  OUT OABL_BUFFER *Out
);

#endif
