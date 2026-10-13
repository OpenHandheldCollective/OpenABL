/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DeviceTree - devicetree inspection and in-place editing.
*/

#include "Core/DeviceTree.h"

#include <limits.h>

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Core/LibFdt.h"

STATIC OABL_TEXT
AsStr (IN OABL_BYTES Bytes)
{
  return (OABL_TEXT){(CONST CHAR8 *)Bytes.Ptr, Bytes.Len};
}

STATIC BOOLEAN
BytesIs (
  IN OABL_BYTES   Bytes,
  IN CONST CHAR8 *String
)
{
  return TextEqStr (AsStr (Bytes), String);
}

STATIC UINTN
Align4 (IN UINTN Value)
{
  return (Value + 3) & ~(UINTN)3;
}

// Gets the NUL-terminated string at offset At, if it ends within Size.
STATIC BOOLEAN
Cstr (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  IN UINTN        At,
  OUT OABL_BYTES *String
)
{
  if (At > Size) {
    return FALSE;
  }
  for (UINTN End = At; End < Size; End++) {
    if (!Bytes[End]) {
      *String = (OABL_BYTES){Bytes + At, End - At};
      return TRUE;
    }
  }
  return FALSE;
}

// Returns the next NUL-separated piece of Value. n NULs give n + 1 pieces.
// Start with *At = 0.
STATIC BOOLEAN
NextPiece (
  IN CONST UINT8 *Value,
  IN UINTN        Size,
  IN OUT UINTN   *At,
  OUT OABL_BYTES *Piece
)
{
  UINTN End = *At;

  if (End > Size) {
    return FALSE;
  }
  while (End < Size && Value[End]) {
    End++;
  }
  *Piece = (OABL_BYTES){Value + *At, End - *At};
  *At    = End + 1;
  return TRUE;
}

// Copies Bytes up to the first NUL. Returns NULL if that isn't UTF-8.
STATIC CHAR8 *
StringOf (IN OABL_BYTES Bytes)
{
  UINTN Length = 0;

  while (Length < Bytes.Len && Bytes.Ptr[Length]) {
    Length++;
  }
  if (!Utf8Valid (Bytes.Ptr, Length)) {
    return NULL;
  }
  return TextDup ((OABL_TEXT){(CONST CHAR8 *)Bytes.Ptr, Length});
}

BOOLEAN
DtInspect (
  IN CONST UINT8 *Blob,
  IN UINTN        Size,
  OUT DT_INFO    *Out OPTIONAL
)
{
  // One entry per open node: whether its name starts with "panel".
  OABL_BUFFER Panels     = {0};
  OABL_BYTES  Model      = {0};
  OABL_BYTES  Compatible = {0};
  OABL_BYTES  Name;
  OABL_BYTES  Value;
  BOOLEAN     HasModel      = FALSE;
  BOOLEAN     HasCompatible = FALSE;
  BOOLEAN     HasRotation   = FALSE;
  BOOLEAN     Ok            = FALSE;
  UINT64      Rotation      = 0;
  UINT32      Word;
  UINT32      Token;
  UINT32      Length;
  UINT32      NameOffset;
  UINTN       Total;
  UINTN       At;
  UINTN       Strings;

  if (!GetBe32 (Blob, Size, 0, &Word) || Word != FDT_MAGIC ||
      !GetBe32 (Blob, Size, 4, &Word) || Word > Size) {
    return FALSE;
  }
  Total = Word;
  if (!GetBe32 (Blob, Total, 8, &Word) || Word > Total) {
    return FALSE;
  }
  At = Word;
  if (!GetBe32 (Blob, Total, 12, &Word) || Word > Total) {
    return FALSE;
  }
  Strings = Word;
  for (;;) {
    if (!GetBe32 (Blob, Total, At, &Token)) {
      goto Done;
    }
    At += 4;
    if (Token == FDT_BEGIN_NODE) {
      if (!Cstr (Blob, Total, At, &Name) ||
          !BufferAppendU8 (&Panels, TextStartsWith (AsStr (Name), "panel"))) {
        goto Done;
      }
      At += Align4 (Name.Len + 1);
    } else if (Token == FDT_END_NODE) {
      if (!Panels.Len) {
        goto Done;
      }
      Panels.Len--;
    } else if (Token == FDT_PROP) {
      if (!GetBe32 (Blob, Total, At, &Length) ||
          !GetBe32 (Blob, Total, At + 4, &NameOffset) ||
          NameOffset > Total - Strings ||
          !Cstr (Blob, Total, Strings + NameOffset, &Name) ||
          !InBounds (Total, At + 8, Length)) {
        goto Done;
      }
      Value = (OABL_BYTES){Blob + At + 8, Length};
      At += 8 + Align4 (Length);
      if (Panels.Len == 1) {
        if (BytesIs (Name, "model") && !HasModel) {
          Model    = Value;
          HasModel = TRUE;
        } else if (BytesIs (Name, "compatible") && !HasCompatible) {
          Compatible    = Value;
          HasCompatible = TRUE;
        }
      } else if (BytesIs (Name, "rotation") && !HasRotation && Panels.Len &&
                 Panels.Data[Panels.Len - 1]) {
        if (Length == 4) {
          Rotation    = ReadBe32 (Value.Ptr);
          HasRotation = TRUE;
        } else if (Length == 8) {
          Rotation    = ReadBe64 (Value.Ptr);
          HasRotation = TRUE;
        }
      }
    } else if (Token == FDT_END) {
      break;
    } else if (Token != FDT_NOP) {
      goto Done;
    }
  }
  if (!HasModel) {
    goto Done;
  }
  if (!Out) {
    UINTN End = 0;
    while (End < Model.Len && Model.Ptr[End]) {
      End++;
    }
    Ok = Utf8Valid (Model.Ptr, End);
    goto Done;
  }
  *Out       = (DT_INFO){0};
  Out->Model = StringOf (Model);
  if (!Out->Model) {
    goto Done;
  }
  Out->Compatible = HasCompatible ? StringOf (Compatible) : NULL;
  if (HasRotation && Rotation % 90 == 0) {
    Out->HasPanelRotation = TRUE;
    Out->PanelRotation    = (UINT16)(Rotation % 360);
  }
  Ok = TRUE;
Done:
  BufferFree (&Panels);
  return Ok;
}

VOID
DtInfoFree (IN OUT DT_INFO *Info)
{
  FreePool (Info->Model);
  FreePool (Info->Compatible);
  *Info = (DT_INFO){0};
}

// Validation and libfdt wrappers

// For WalkOk: no node to check.
#define NO_NODE MAX_UINTN

// Checks for version 17 or later, the structure then the strings block
// inside totalsize, and totalsize inside the buffer and libfdt's int range.
STATIC BOOLEAN
HeaderOk (
  IN CONST VOID *Fdt,
  IN UINTN       Size
)
{
  UINT64 Total;
  UINT64 StructOffset;
  UINT64 StringsOffset;

  if (Size < sizeof (struct fdt_header) || fdt_magic (Fdt) != FDT_MAGIC ||
      fdt_version (Fdt) < 17) {
    return FALSE;
  }
  Total         = fdt_totalsize (Fdt);
  StructOffset  = fdt_off_dt_struct (Fdt);
  StringsOffset = fdt_off_dt_strings (Fdt);
  return Total <= Size && Total <= INT_MAX &&
         StructOffset + fdt_size_dt_struct (Fdt) <= StringsOffset &&
         StringsOffset + fdt_size_dt_strings (Fdt) <= Total;
}

// Checks the structure block before libfdt sees it. This libfdt checks
// neither name offsets nor property lengths, and a length of 0xfffffff4
// wraps its int offsets back to the property's own tag, so it loops forever.
// Node, unless NO_NODE, must be a begin tag. Call after HeaderOk.
STATIC BOOLEAN
WalkOk (
  IN CONST UINT8 *Fdt,
  IN UINTN        Node
)
{
  CONST UINT8 *Struct  = Fdt + fdt_off_dt_struct (Fdt),
              *Strings = Fdt + fdt_off_dt_strings (Fdt);
  UINTN   StructSize   = fdt_size_dt_struct (Fdt);
  UINTN   StringsSize  = fdt_size_dt_strings (Fdt);
  UINTN   At           = 0;
  UINTN   Depth        = 0;
  UINTN   End;
  BOOLEAN Seen = Node == NO_NODE;
  UINT32  Tag;
  UINT32  Length;
  UINT32  NameOffset;

  for (;;) {
    if (At > StructSize || StructSize - At < 4) {
      return FALSE;
    }
    Tag = ReadBe32 (Struct + At);
    if (Tag == FDT_BEGIN_NODE) {
      for (End = At + 4; End < StructSize && Struct[End]; End++)
        ;
      if (End == StructSize) {
        return FALSE;
      }
      Seen |= At == Node;
      Depth++;
      At = Align4 (End + 1);
    } else if (Tag == FDT_END_NODE) {
      if (!Depth--) {
        return FALSE;
      }
      At += 4;
    } else if (Tag == FDT_PROP) {
      if (StructSize - At < 12) {
        return FALSE;
      }
      Length     = ReadBe32 (Struct + At + 4);
      NameOffset = ReadBe32 (Struct + At + 8);
      if (Length > StructSize - At - 12 || NameOffset >= StringsSize) {
        return FALSE;
      }
      for (End = NameOffset; End < StringsSize && Strings[End]; End++)
        ;
      if (End == StringsSize) {
        return FALSE;
      }
      At = Align4 (At + 12 + Length);
    } else if (Tag == FDT_NOP) {
      At += 4;
    } else if (Tag == FDT_END) {
      return !Depth && Seen;
    } else {
      return FALSE;
    }
  }
}

// Returns TRUE if libfdt's reads will stay inside the blob and its walks end.
STATIC BOOLEAN
Usable (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        Node
)
{
  return HeaderOk (Fdt, Size) && WalkOk (Fdt, Node);
}

STATIC DT_ERROR
DtError (IN INT32 Status)
{
  if (Status >= 0) {
    return DT_OK;
  }
  return Status == -FDT_ERR_NOSPACE ? DT_NO_SPACE : DT_BAD_BLOB;
}

// Thin libfdt wrappers. The blob must have passed Usable.

STATIC BOOLEAN
Get (
  IN CONST UINT8 *Fdt,
  IN UINTN        Node,
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
)
{
  CONST VOID *Property;
  INT32       Length;

  if (!(Property = fdt_getprop (Fdt, (INT32)Node, Name, &Length)) ||
      Length < 0) {
    return FALSE;
  }
  *Value = (OABL_BYTES){Property, (UINTN)Length};
  return TRUE;
}

STATIC BOOLEAN
AtPath (
  IN CONST UINT8 *Fdt,
  IN CONST CHAR8 *Path,
  OUT UINTN      *Node
)
{
  INT32 Offset = fdt_path_offset (Fdt, Path);

  if (Offset < 0) {
    return FALSE;
  }
  *Node = (UINTN)Offset;
  return TRUE;
}

STATIC DT_ERROR
Set (
  IN OUT UINT8   *Fdt,
  IN UINTN        Node,
  IN CONST CHAR8 *Name,
  IN CONST VOID  *Value,
  IN UINTN        Length
)
{
  if (Length > INT_MAX) {
    return DT_NO_SPACE;
  }
  return DtError (fdt_setprop (Fdt, (INT32)Node, Name, Value, (INT32)Length));
}

STATIC DT_ERROR
Add (
  IN OUT UINT8   *Fdt,
  IN UINTN        Parent,
  IN CONST CHAR8 *Name,
  OUT UINTN      *Node
)
{
  INT32 Offset = fdt_add_subnode (Fdt, (INT32)Parent, Name);

  if (Offset < 0) {
    return DtError (Offset);
  }
  *Node = (UINTN)Offset;
  return DT_OK;
}

STATIC BOOLEAN
Where (
  IN CONST UINT8 *Fdt,
  IN BOOLEAN (*Want) (
    IN CONST UINT8 *Compatible,
    IN UINTN        Length,
    IN VOID        *Context
  ),
  IN VOID         *Context,
  IN OUT DT_NODES *Found
)
{
  for (INT32 Offset = fdt_next_node (Fdt, -1, NULL); Offset >= 0;
       Offset       = fdt_next_node (Fdt, Offset, NULL)) {
    OABL_BYTES Compatible;
    OABL_BYTES Piece;
    BOOLEAN    Any = FALSE;

    if (!Get (Fdt, (UINTN)Offset, "compatible", &Compatible)) {
      continue;
    }
    for (UINTN At = 0;
         !Any && NextPiece (Compatible.Ptr, Compatible.Len, &At, &Piece);) {
      Any = Piece.Len && Want (Piece.Ptr, Piece.Len, Context);
    }
    if (Any && !VecPush (Found, (UINTN)Offset)) {
      return FALSE;
    }
  }
  return TRUE;
}

// Returns the node's name, or "" if it can't be read.
STATIC OABL_TEXT
NameOf (
  IN CONST VOID *Fdt,
  IN INT32       Node
)
{
  INT32        Length;
  CONST CHAR8 *Name = fdt_get_name (Fdt, Node, &Length);

  return Name ? (OABL_TEXT){Name, (UINTN)Length} : (OABL_TEXT){"", 0};
}

DT_ERROR
DtSetPropByCompatible (
  IN OUT UINT8          *Fdt,
  IN UINTN               Capacity,
  IN CONST CHAR8 *CONST *Compatibles,
  IN UINTN               Count,
  IN CONST CHAR8        *Name,
  IN CONST VOID         *Value,
  IN UINTN               Length
)
{
  INT32 Node;

  // An unreadable header gives DT_NOT_FOUND, not DT_BAD_BLOB.
  if (!HeaderOk (Fdt, Capacity)) {
    return DT_NOT_FOUND;
  }
  if (!WalkOk (Fdt, NO_NODE)) {
    return DT_BAD_BLOB;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    if ((Node = fdt_node_offset_by_compatible (Fdt, -1, Compatibles[Index])) >=
        0) {
      return Set (Fdt, (UINTN)Node, Name, Value, Length);
    }
  }
  return DT_NOT_FOUND;
}

DT_ERROR
DtSetPropAt (
  IN OUT UINT8   *Fdt,
  IN UINTN        Capacity,
  IN UINTN        Node,
  IN CONST CHAR8 *Name,
  IN CONST VOID  *Value,
  IN UINTN        Length
)
{
  if (!Usable (Fdt, Capacity, Node)) {
    return DT_BAD_BLOB;
  }
  return Set (Fdt, Node, Name, Value, Length);
}

// Lookups and subnodes

BOOLEAN
DtNodeAtPath (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN CONST CHAR8 *Path,
  OUT UINTN      *Node
)
{
  return Usable (Fdt, Size, NO_NODE) && AtPath (Fdt, Path, Node);
}

BOOLEAN
DtNodeName (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        Node,
  OUT OABL_BYTES *Name
)
{
  CONST CHAR8 *NodeName;
  INT32        Length;

  if (!Usable (Fdt, Size, Node) ||
      !(NodeName = fdt_get_name (Fdt, (INT32)Node, &Length))) {
    return FALSE;
  }
  *Name = (OABL_BYTES){(CONST UINT8 *)NodeName, (UINTN)Length};
  return TRUE;
}

BOOLEAN
DtPropAt (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        Node,
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
)
{
  return Usable (Fdt, Size, Node) && Get (Fdt, Node, Name, Value);
}

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
)
{
  *Found = (DT_NODES){0};
  return !Usable (Fdt, Size, NO_NODE) || Where (Fdt, Want, Context, Found);
}

DT_ERROR
DtAddSubnode (
  IN OUT UINT8   *Fdt,
  IN UINTN        Capacity,
  IN UINTN        Parent,
  IN CONST CHAR8 *Name,
  OUT UINTN      *Node
)
{
  if (!Usable (Fdt, Capacity, Parent)) {
    return DT_BAD_BLOB;
  }
  return Add (Fdt, Parent, Name, Node);
}

BOOLEAN
DtChosenHas (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN CONST CHAR8 *Prefix
)
{
  if (!Usable (Fdt, Size, NO_NODE)) {
    return FALSE;
  }
  // Every child of the root named chosen, but not chosen@...
  for (INT32 Child = fdt_first_subnode (Fdt, 0); Child >= 0;
       Child       = fdt_next_subnode (Fdt, Child)) {
    if (!TextEqStr (NameOf (Fdt, Child), "chosen")) {
      continue;
    }
    for (INT32 Grandchild = fdt_first_subnode (Fdt, Child); Grandchild >= 0;
         Grandchild       = fdt_next_subnode (Fdt, Grandchild)) {
      if (TextStartsWith (NameOf (Fdt, Grandchild), Prefix)) {
        return TRUE;
      }
    }
  }
  return FALSE;
}

BOOLEAN
DtNodeChildren (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        Node,
  OUT DT_NODES   *Out
)
{
  *Out = (DT_NODES){0};
  if (!Usable (Fdt, Size, Node)) {
    return TRUE;
  }
  for (INT32 Child = fdt_first_subnode (Fdt, (INT32)Node); Child >= 0;
       Child       = fdt_next_subnode (Fdt, Child)) {
    if (!VecPush (Out, (UINTN)Child)) {
      return FALSE;
    }
  }
  return TRUE;
}

STATIC CHAR8 *
UnitName (
  IN CONST CHAR8 *Prefix,
  IN UINT64       Address
)
{
  return SPrintAlloc ("%s@%llx", Prefix, (UINT64)Address);
}

// Reads #address-cells or #size-cells. Fails unless it is 4 bytes.
STATIC BOOLEAN
CellsOf (
  IN CONST UINT8 *Fdt,
  IN UINTN        Node,
  IN CONST CHAR8 *Name,
  OUT UINT32     *Cells
)
{
  OABL_BYTES Property;

  if (!Get (Fdt, Node, Name, &Property) || Property.Len != 4) {
    return FALSE;
  }
  *Cells = ReadBe32 (Property.Ptr);
  return TRUE;
}

STATIC UINT64
ReadCells (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size
)
{
  UINT64 Value = 0;

  for (UINTN Index = 0; Index < Size; Index++) {
    Value = Value << 8 | Bytes[Index];
  }
  return Value;
}

// Writes Value as 1 or 2 cells and returns the bytes written.
STATIC UINTN
PushCells (
  OUT UINT8 *Out,
  IN UINT64  Value,
  IN UINT32  Cells
)
{
  if (Cells == 1) {
    WriteBe32 (Out, (UINT32)Value);
    return 4;
  }
  WriteBe32 (Out, (UINT32)(Value >> 32));
  WriteBe32 (Out + 4, (UINT32)Value);
  return 8;
}

DT_ERROR
DtReserveMemory (
  IN OUT UINT8 *Fdt,
  IN UINTN      Capacity,
  IN UINT64     Base,
  IN UINT64     Size,
  OUT BOOLEAN  *Added
)
{
  return DtReserveMemoryNamed (Fdt, Capacity, "framebuffer", Base, Size, Added);
}

DT_ERROR
DtReserveMemoryNamed (
  IN OUT UINT8   *Fdt,
  IN UINTN        Capacity,
  IN CONST CHAR8 *Prefix,
  IN UINT64       Base,
  IN UINT64       Size,
  OUT BOOLEAN    *Added
)
{
  UINTN      Root;
  UINTN      ReservedMemory;
  UINTN      Node;
  UINTN      AddressBytes;
  UINTN      SizeBytes;
  UINTN      Length;
  UINT32     AddressCells = 2;
  UINT32     SizeCells    = 1;
  DT_ERROR   Status;
  OABL_BYTES Reg;
  UINT8      Buffer[16];
  CHAR8     *Name;

  *Added = FALSE;
  if (!Usable (Fdt, Capacity, NO_NODE) || !AtPath (Fdt, "/", &Root)) {
    return DT_BAD_BLOB;
  }
  CellsOf (Fdt, Root, "#address-cells", &AddressCells);
  CellsOf (Fdt, Root, "#size-cells", &SizeCells);
  if (!AtPath (Fdt, "/reserved-memory", &ReservedMemory)) {
    if ((Status = Add (Fdt, Root, "reserved-memory", &ReservedMemory)) ||
        (Status = Set (Fdt, ReservedMemory, "ranges", "", 0))) {
      return Status;
    }
    WriteBe32 (Buffer, SizeCells);
    if ((Status = Set (Fdt, ReservedMemory, "#size-cells", Buffer, 4))) {
      return Status;
    }
    WriteBe32 (Buffer, AddressCells);
    if ((Status = Set (Fdt, ReservedMemory, "#address-cells", Buffer, 4))) {
      return Status;
    }
  }
  CellsOf (Fdt, ReservedMemory, "#address-cells", &AddressCells);
  CellsOf (Fdt, ReservedMemory, "#size-cells", &SizeCells);
  if (AddressCells < 1 || AddressCells > 2 || SizeCells < 1 || SizeCells > 2) {
    return DT_BAD_BLOB;
  }
  AddressBytes = AddressCells * 4;
  SizeBytes    = SizeCells * 4;
  for (INT32 Child = fdt_first_subnode (Fdt, (INT32)ReservedMemory); Child >= 0;
       Child       = fdt_next_subnode (Fdt, Child)) {
    if (!Get (Fdt, (UINTN)Child, "reg", &Reg)) {
      continue;
    }
    for (UINTN At = 0; At + AddressBytes + SizeBytes <= Reg.Len;
         At += AddressBytes + SizeBytes) {
      UINT64 RegionBase = ReadCells (Reg.Ptr + At, AddressBytes);
      UINT64 RegionSize = ReadCells (Reg.Ptr + At + AddressBytes, SizeBytes);
      if (RegionBase <= Base && Base + Size <= RegionBase + RegionSize) {
        return DT_OK;
      }
    }
  }
  Name = UnitName (Prefix, Base);
  if (!Name) {
    return DT_NO_SPACE;
  }
  Status = Add (Fdt, ReservedMemory, Name, &Node);
  FreePool (Name);
  if (Status || (Status = Set (Fdt, Node, "no-map", "", 0))) {
    return Status;
  }
  Length = PushCells (Buffer, Base, AddressCells);
  Length += PushCells (Buffer + Length, Size, SizeCells);
  if ((Status = Set (Fdt, Node, "reg", Buffer, Length))) {
    return Status;
  }
  *Added = TRUE;
  return DT_OK;
}

// Splash framebuffer hand-over

BOOLEAN
DtIsDisplayNode (
  IN CONST UINT8 *Compatible,
  IN UINTN        Length
)
{
  OABL_TEXT Text = {(CONST CHAR8 *)Compatible, Length};

  return TextEndsWith (Text, "-mdss") || TextEndsWith (Text, "-dpu") ||
         TextEndsWith (Text, "-dsi-ctrl") || TextContains (Text, "dsi-phy");
}

STATIC BOOLEAN
DisplayNode (
  IN CONST UINT8 *Compatible,
  IN UINTN        Length,
  IN VOID        *Context
)
{
  (VOID)Context;
  return DtIsDisplayNode (Compatible, Length);
}

// Byte length of each clock reference gathered, in order.
typedef struct _CLOCK_LENGTHS
OABL_VEC (UINTN) CLOCK_LENGTHS;

// Returns TRUE if Clock is already among the references in Clocks.
STATIC BOOLEAN
HasClock (
  IN CONST OABL_BUFFER   *Clocks,
  IN CONST CLOCK_LENGTHS *Lengths,
  IN CONST UINT8         *Clock,
  IN UINTN                Length
)
{
  UINTN At = 0;

  for (UINTN Index = 0; Index < Lengths->Count; At += Lengths->Items[Index++]) {
    if (Lengths->Items[Index] == Length &&
        CompareMem (Clocks->Data + At, Clock, Length) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

// Returns #clock-cells of the provider with this phandle, or 1 if unknown.
// Every Qualcomm display clock provider uses 1.
STATIC UINT32
ClockCells (
  IN CONST UINT8 *Fdt,
  IN UINT32       Phandle
)
{
  for (INT32 Offset = fdt_next_node (Fdt, -1, NULL); Offset >= 0;
       Offset       = fdt_next_node (Fdt, Offset, NULL)) {
    OABL_BYTES Value;

    if ((Get (Fdt, (UINTN)Offset, "phandle", &Value) ||
         Get (Fdt, (UINTN)Offset, "linux,phandle", &Value)) &&
        Value.Len == 4 && ReadBe32 (Value.Ptr) == Phandle) {
      return Get (Fdt, (UINTN)Offset, "#clock-cells", &Value) && Value.Len == 4
               ? ReadBe32 (Value.Ptr)
               : 1;
    }
  }
  return 1;
}

// Adds node Name to /chosen, or returns the existing one: Retroid's SM8250
// trees already have framebuffer@9c000000, without the display clocks.
STATIC DT_ERROR
FramebufferNode (
  IN OUT UINT8   *Fdt,
  IN UINTN        Chosen,
  IN CONST CHAR8 *Name,
  OUT UINTN      *Node
)
{
  INT32 Offset;

  if ((Offset = fdt_add_subnode (Fdt, (INT32)Chosen, Name)) ==
      -FDT_ERR_EXISTS) {
    for (Offset = fdt_first_subnode (Fdt, (INT32)Chosen); Offset >= 0;
         Offset = fdt_next_subnode (Fdt, Offset)) {
      if (TextEqStr (NameOf (Fdt, Offset), Name)) {
        break;
      }
    }
  }
  if (Offset < 0) {
    return DtError (Offset);
  }
  *Node = (UINTN)Offset;
  return DT_OK;
}

DT_ERROR
DtAddFramebuffer (
  IN OUT UINT8            *Fdt,
  IN UINTN                 Capacity,
  IN CONST DT_FRAMEBUFFER *Framebuffer,
  OUT DT_FRAMEBUFFER_NODE *Out
)
{
  OABL_BUFFER   Clocks       = {0};
  CLOCK_LENGTHS Lengths      = {0};
  OABL_BUFFER   Power        = {0};
  DT_ERROR      Status       = DT_NO_SPACE;
  DT_NODES      DisplayNodes = {0};
  OABL_BYTES    Property;
  OABL_BYTES    Piece;
  BOOLEAN       HasPower = FALSE;
  UINTN         Root;
  UINTN         Chosen;
  UINTN         Node;
  CHAR8        *Name = NULL;
  UINT8         Buffer[16];

  if (!Usable (Fdt, Capacity, NO_NODE)) {
    Status = DT_BAD_BLOB;
    goto Done;
  }
  if (!Where (Fdt, DisplayNode, NULL, &DisplayNodes)) {
    goto Done;
  }
  for (UINTN Index = 0; Index < DisplayNodes.Count; Index++) {
    UINTN   Display = DisplayNodes.Items[Index];
    BOOLEAN IsMdss  = FALSE;

    // Skip disabled nodes, such as the second DSI link on a one-panel
    // device. Their clocks must not be held on.
    if (Get (Fdt, Display, "status", &Property) &&
        TextStartsWith (AsStr (Property), "disabled")) {
      continue;
    }
    // Each reference is a phandle and #clock-cells cells. A truncated
    // reference ends the list.
    if (Get (Fdt, Display, "clocks", &Property)) {
      for (UINTN At = 0; At + 4 <= Property.Len;) {
        UINT64 Length =
          4 + 4 * (UINT64)ClockCells (Fdt, ReadBe32 (Property.Ptr + At));

        if (Length > Property.Len - At) {
          break;
        }
        if (!HasClock (&Clocks, &Lengths, Property.Ptr + At, (UINTN)Length) &&
            (!BufferAppend (&Clocks, Property.Ptr + At, (UINTN)Length) ||
             !VecPush (&Lengths, (UINTN)Length))) {
          goto Done;
        }
        At += (UINTN)Length;
      }
    }
    if (Get (Fdt, Display, "compatible", &Property)) {
      for (UINTN At = 0;
           !IsMdss && NextPiece (Property.Ptr, Property.Len, &At, &Piece);) {
        IsMdss = TextEndsWith (AsStr (Piece), "-mdss");
      }
    }
    if (!HasPower && IsMdss && Get (Fdt, Display, "power-domains", &Property)) {
      if (!BufferFrom (&Power, Property.Ptr, Property.Len)) {
        goto Done;
      }
      HasPower = TRUE;
    }
  }
  Status = DT_BAD_BLOB;
  if (!AtPath (Fdt, "/", &Root)) {
    goto Done;
  }
  if (!AtPath (Fdt, "/chosen", &Chosen) &&
      (Status = Add (Fdt, Root, "chosen", &Chosen))) {
    goto Done;
  }
  WriteBe32 (Buffer, 2);
  if ((Status = Set (Fdt, Chosen, "#address-cells", Buffer, 4)) ||
      (Status = Set (Fdt, Chosen, "#size-cells", Buffer, 4)) ||
      (Status = Set (Fdt, Chosen, "ranges", "", 0))) {
    goto Done;
  }
  Status = DT_NO_SPACE;
  if (!(Name = UnitName ("framebuffer", Framebuffer->Base)) ||
      (Status = FramebufferNode (Fdt, Chosen, Name, &Node))) {
    goto Done;
  }
  // libfdt puts a new property first, so set these in reverse to get the
  // usual order.
  if (HasPower &&
      (Status = Set (Fdt, Node, "power-domains", Power.Data, Power.Len))) {
    goto Done;
  }
  if (Clocks.Len &&
      (Status = Set (Fdt, Node, "clocks", Clocks.Data, Clocks.Len))) {
    goto Done;
  }
  if ((Status = Set (
         Fdt, Node, "format", Framebuffer->Format,
         AsciiStrLen (Framebuffer->Format) + 1
       ))) {
    goto Done;
  }
  WriteBe32 (Buffer, Framebuffer->Stride);
  if ((Status = Set (Fdt, Node, "stride", Buffer, 4))) {
    goto Done;
  }
  WriteBe32 (Buffer, Framebuffer->Height);
  if ((Status = Set (Fdt, Node, "height", Buffer, 4))) {
    goto Done;
  }
  WriteBe32 (Buffer, Framebuffer->Width);
  if ((Status = Set (Fdt, Node, "width", Buffer, 4))) {
    goto Done;
  }
  PushCells (Buffer, Framebuffer->Base, 2);
  PushCells (Buffer + 8, Framebuffer->Size, 2);
  if ((Status = Set (Fdt, Node, "reg", Buffer, 16)) ||
      (Status = Set (
         Fdt, Node, "compatible", "simple-framebuffer",
         sizeof ("simple-framebuffer")
       ))) {
    goto Done;
  }
  *Out = (DT_FRAMEBUFFER_NODE){Lengths.Count, HasPower};
Done:
  FreePool (Name);
  BufferFree (&Power);
  BufferFree (&Clocks);
  VecFree (&Lengths);
  VecFree (&DisplayNodes);
  return Status;
}
