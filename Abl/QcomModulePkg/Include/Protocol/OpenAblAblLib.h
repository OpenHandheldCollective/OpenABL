/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-source-exception */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/*
 * OpenAblAblLib - Qualcomm's decompress and libfdt, offered to the OpenABL
 * program so it doesn't need its own copies.
 */

#ifndef __OPENABL_ABL_LIB_H__
#define __OPENABL_ABL_LIB_H__

#define OPENABL_ABL_LIB_PROTOCOL_GUID                                          \
  {                                                                            \
    0x71677a04, 0xd74e, 0x40e1,                                                \
    {                                                                          \
      0x9c, 0x4f, 0xc8, 0xf4, 0x6a, 0xa7, 0x43, 0xf8                           \
    }                                                                          \
  }

typedef struct _OPENABL_ABL_LIB_PROTOCOL OPENABL_ABL_LIB_PROTOCOL;

typedef EFI_STATUS (EFIAPI *OPENABL_ABL_LIB_INFLATE) (
    IN CONST OPENABL_ABL_LIB_PROTOCOL *This, IN CONST VOID *In, IN UINTN InSize,
    OUT VOID *Out, IN UINTN OutSize, OUT UINTN *Written, OUT UINTN *Used);

struct _OPENABL_ABL_LIB_PROTOCOL {
  OPENABL_ABL_LIB_INFLATE Inflate;
  INT32 (*fdt_path_offset) (CONST VOID *Fdt, CONST CHAR8 *Path);
  CONST CHAR8 *(*fdt_get_name) (CONST VOID *Fdt, INT32 NodeOffset,
                                INT32 *Len);
  CONST VOID *(*fdt_getprop) (CONST VOID *Fdt, INT32 NodeOffset,
                              CONST CHAR8 *Name, INT32 *Len);
  INT32 (*fdt_next_node) (CONST VOID *Fdt, INT32 Offset, INT32 *Depth);
  INT32 (*fdt_first_subnode) (CONST VOID *Fdt, INT32 Offset);
  INT32 (*fdt_next_subnode) (CONST VOID *Fdt, INT32 Offset);
  INT32 (*fdt_node_offset_by_compatible) (CONST VOID *Fdt, INT32 StartOffset,
                                          CONST CHAR8 *Compatible);
  /* The edits. */
  INT32 (*fdt_setprop) (VOID *Fdt, INT32 NodeOffset, CONST CHAR8 *Name,
                        CONST VOID *Value, INT32 Len);
  INT32 (*fdt_add_subnode) (VOID *Fdt, INT32 ParentOffset, CONST CHAR8 *Name);
};

#endif /* __OPENABL_ABL_LIB_H__ */
