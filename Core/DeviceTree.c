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

BOOLEAN
DtFindAll (
  IN CONST UINT8 *Region,
  IN UINTN        Size,
  OUT DT_BLOBS   *Found
)
{
  STATIC CONST UINT8 Magic[4] = {0xd0, 0x0d, 0xfe, 0xed};
  UINTN              From     = 0;

  *Found = (DT_BLOBS){0};
  while (From + 8 <= Size) {
    UINTN At    = From;
    UINTN Total = 0;

    while (At + 4 <= Size && CompareMem (Region + At, Magic, 4) != 0) {
      At++;
    }
    if (At + 4 > Size) {
      break;
    }
    if (At + 8 <= Size) {
      Total = ReadBe32 (Region + At + 4);
    }
    if (Total >= 64 && Total <= 16 << 20 && Total <= Size - At &&
        DtInspect (Region + At, Total, NULL)) {
      if (!VecPush (Found, (DT_BLOB){At, Total})) {
        return FALSE;
      }
      From = At + Total;
    } else {
      From = At + 4;
    }
  }
  return TRUE;
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

#define MAX_DT_DEPTH 32

STATIC BOOLEAN
HasPropStr (
  IN CONST UINT8 *Fdt,
  IN UINTN        Node,
  IN CONST CHAR8 *PropName,
  IN CONST CHAR8 *Target
)
{
  OABL_BYTES Prop;
  UINTN      TargetLen = AsciiStrLen (Target);
  UINTN      At        = 0;

  if (!Get (Fdt, Node, PropName, &Prop)) {
    return FALSE;
  }
  while (At < Prop.Len) {
    UINTN End = At;

    while (End < Prop.Len && Prop.Ptr[End]) {
      End++;
    }
    if (End - At == TargetLen &&
        CompareMem (Prop.Ptr + At, Target, TargetLen) == 0) {
      return TRUE;
    }
    At = End + 1;
  }
  return FALSE;
}

STATIC BOOLEAN
NodeEnabled (
  IN CONST UINT8 *Fdt,
  IN UINTN        Node
)
{
  OABL_BYTES Status;

  if (!Get (Fdt, Node, "status", &Status)) {
    return TRUE;
  }
  return HasPropStr (Fdt, Node, "status", "okay") ||
         HasPropStr (Fdt, Node, "status", "ok");
}

STATIC BOOLEAN
NodeByPhandle (
  IN CONST UINT8 *Fdt,
  IN UINT32       Phandle,
  OUT UINTN      *Out
)
{
  INT32 Node  = -1;
  INT32 Depth = -1;

  if (!Phandle || Phandle == 0xFFFFFFFFU) {
    return FALSE;
  }
  for (;;) {
    OABL_BYTES Prop;

    Node = fdt_next_node (Fdt, Node, &Depth);
    if (Node < 0 || Depth < 0) {
      return FALSE;
    }
    if ((Get (Fdt, (UINTN)Node, "phandle", &Prop) ||
         Get (Fdt, (UINTN)Node, "linux,phandle", &Prop)) &&
        Prop.Len == 4 && ReadBe32 (Prop.Ptr) == Phandle) {
      *Out = (UINTN)Node;
      return TRUE;
    }
  }
}

STATIC UINT16
GpioPinOf (
  IN CONST UINT8 *Fdt,
  IN UINTN        Node,
  IN CONST CHAR8 *PropName
)
{
  OABL_BYTES Prop;
  UINT32     Pin;

  if (!Get (Fdt, Node, PropName, &Prop) || Prop.Len < 8) {
    return DT_NO_GPIO;
  }
  Pin = ReadBe32 (Prop.Ptr + 4);
  return Pin < 512 ? (UINT16)Pin : DT_NO_GPIO;
}

STATIC UINT16
SupplyGpioPinOf (
  IN CONST UINT8 *Fdt,
  IN UINTN        Node,
  IN CONST CHAR8 *SupplyName
)
{
  OABL_BYTES Prop;
  UINTN      RegNode;
  UINT16     Pin;

  if (!Get (Fdt, Node, SupplyName, &Prop) || Prop.Len < 4 ||
      !NodeByPhandle (Fdt, ReadBe32 (Prop.Ptr), &RegNode)) {
    return DT_NO_GPIO;
  }
  Pin = GpioPinOf (Fdt, RegNode, "gpio");
  if (Pin == DT_NO_GPIO) {
    Pin = GpioPinOf (Fdt, RegNode, "gpios");
  }
  return Pin;
}

STATIC BOOLEAN
ParseGpioName (
  IN CONST UINT8 *Str,
  IN UINTN        Len,
  OUT UINT16     *Pin
)
{
  UINT32 Value = 0;

  if (Len < 5 || Len > 7 || CompareMem (Str, "gpio", 4) != 0) {
    return FALSE;
  }
  for (UINTN Index = 4; Index < Len; Index++) {
    if (Str[Index] < '0' || Str[Index] > '9') {
      return FALSE;
    }
    Value = Value * 10 + (UINT32)(Str[Index] - '0');
  }
  if (Value >= 512) {
    return FALSE;
  }
  *Pin = (UINT16)Value;
  return TRUE;
}

STATIC VOID
CollectPinsInNode (
  IN CONST UINT8 *Fdt,
  IN UINTN        Node,
  IN OUT UINT16   Pins[2],
  IN OUT UINTN   *Count
)
{
  OABL_BYTES Prop;
  UINTN      At = 0;

  if (!Get (Fdt, Node, "pins", &Prop)) {
    return;
  }
  while (At < Prop.Len && *Count < 2) {
    UINTN  End = At;
    UINT16 Pin;

    while (End < Prop.Len && Prop.Ptr[End]) {
      End++;
    }
    if (ParseGpioName (Prop.Ptr + At, End - At, &Pin)) {
      Pins[(*Count)++] = Pin;
    }
    At = End + 1;
  }
}

STATIC BOOLEAN
UartPinsOf (
  IN CONST UINT8 *Fdt,
  IN UINTN        UartNode,
  OUT UINT16     *TxGpio,
  OUT UINT16     *RxGpio
)
{
  OABL_BYTES Prop;
  UINT16     Pins[2] = {DT_NO_GPIO, DT_NO_GPIO};
  UINTN      Count   = 0;

  if (!Get (Fdt, UartNode, "pinctrl-0", &Prop)) {
    return FALSE;
  }
  for (UINTN At = 0; At + 4 <= Prop.Len && Count < 2; At += 4) {
    UINTN PinNode;

    if (!NodeByPhandle (Fdt, ReadBe32 (Prop.Ptr + At), &PinNode)) {
      continue;
    }
    CollectPinsInNode (Fdt, PinNode, Pins, &Count);
    for (INT32 Child = fdt_first_subnode (Fdt, (INT32)PinNode);
         Child >= 0 && Count < 2; Child = fdt_next_subnode (Fdt, Child)) {
      CollectPinsInNode (Fdt, (UINTN)Child, Pins, &Count);
    }
  }
  if (Count < 2) {
    return FALSE;
  }
  *TxGpio = Pins[0];
  *RxGpio = Pins[1];
  return TRUE;
}

BOOLEAN
DtGamepadConfig (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  OUT DT_GAMEPAD *Out
)
{
  UINTN Ancestors[MAX_DT_DEPTH] = {0};
  INT32 Node                    = -1;
  INT32 Depth                   = -1;

  *Out = (DT_GAMEPAD){
    .TxGpio   = DT_NO_GPIO,
    .RxGpio   = DT_NO_GPIO,
    .VddGpio  = DT_NO_GPIO,
    .BootGpio = DT_NO_GPIO,
    .EnGpio   = DT_NO_GPIO,
    .RstGpio  = DT_NO_GPIO,
  };
  if (!Usable (Fdt, Size, NO_NODE)) {
    return FALSE;
  }
  for (;;) {
    BOOLEAN    IsRetroid8250 = FALSE;
    UINTN      UartNode;
    UINT32     AddressCells = 2;
    OABL_BYTES Reg;
    UINT64     UartBase;
    UINT16     TxGpio;
    UINT16     RxGpio;

    Node = fdt_next_node (Fdt, Node, &Depth);
    if (Node < 0 || Depth < 0) {
      return FALSE;
    }
    if (Depth >= MAX_DT_DEPTH) {
      continue;
    }
    Ancestors[Depth] = (UINTN)Node;
    if (Depth < 1 || !NodeEnabled (Fdt, (UINTN)Node)) {
      continue;
    }
    if (HasPropStr (
          Fdt, (UINTN)Node, "compatible", "retroid,retroid-pocket-gamepad"
        )) {
      IsRetroid8250 = TRUE;
    } else if (!HasPropStr (
                 Fdt, (UINTN)Node, "compatible", "gamepad,rsinput"
               )) {
      continue;
    }
    UartNode = Ancestors[Depth - 1];
    if (!NodeEnabled (Fdt, UartNode)) {
      continue;
    }
    if (Depth >= 2) {
      CellsOf (Fdt, Ancestors[Depth - 2], "#address-cells", &AddressCells);
    }
    if (AddressCells < 1 || AddressCells > 2 ||
        !Get (Fdt, UartNode, "reg", &Reg) || Reg.Len < AddressCells * 4) {
      continue;
    }
    UartBase = ReadCells (Reg.Ptr, AddressCells * 4);
    if (!UartBase || !UartPinsOf (Fdt, UartNode, &TxGpio, &RxGpio)) {
      continue;
    }
    Out->UartBase      = UartBase;
    Out->TxGpio        = TxGpio;
    Out->RxGpio        = RxGpio;
    Out->VddGpio       = SupplyGpioPinOf (Fdt, (UINTN)Node, "vdd-supply");
    Out->BootGpio      = GpioPinOf (Fdt, (UINTN)Node, "boot-gpios");
    Out->EnGpio        = GpioPinOf (Fdt, (UINTN)Node, "enable-gpios");
    Out->RstGpio       = GpioPinOf (Fdt, (UINTN)Node, "reset-gpios");
    Out->IsRetroid8250 = IsRetroid8250;
    return Out->EnGpio != DT_NO_GPIO;
  }
}
