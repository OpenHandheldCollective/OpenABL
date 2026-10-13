/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Buffer - growable arrays and byte-order helpers.
*/

#include "Base/Buffer.h"

#include "Base/Memory.h"

#include "Base/OablBase.h"

BOOLEAN
VecGrow (
  IN OUT VOID **Items,
  IN OUT UINTN *Capacity,
  IN UINTN      Need,
  IN UINTN      Size
)
{
  UINTN NewCapacity;
  VOID *Grown;

  if (Need <= *Capacity) {
    return TRUE;
  }
  NewCapacity = *Capacity ? *Capacity * 2 : 8;
  // Double until it holds Need, or take Need itself if doubling would
  // overflow.
  while (NewCapacity < Need) {
    NewCapacity = NewCapacity > MAX_UINTN / 2 ? Need : NewCapacity * 2;
  }
  if (NewCapacity > MAX_UINTN / Size) {
    return FALSE;
  }
  Grown = ResizePool (*Items, NewCapacity * Size);
  if (!Grown) {
    return FALSE;
  }
  *Items    = Grown;
  *Capacity = NewCapacity;
  return TRUE;
}

VOID
VecRemoveRaw (
  IN OUT VOID  *Items,
  IN OUT UINTN *Count,
  IN UINTN      Index,
  IN UINTN      Size
)
{
  UINT8 *Bytes = Items;
  if (Index >= *Count) {
    return;
  }
  for (UINTN At = Index * Size; At < (*Count - 1) * Size; At++) {
    Bytes[At] = Bytes[At + Size];
  }
  (*Count)--;
}

UINTN
VecOpenRaw (
  IN OUT VOID  *Items,
  IN OUT UINTN *Count,
  IN UINTN      Index,
  IN UINTN      Size
)
{
  UINT8 *Bytes = Items;
  if (Index > *Count) {
    Index = *Count;
  }
  for (UINTN At = (*Count + 1) * Size; At-- > (Index + 1) * Size;) {
    Bytes[At] = Bytes[At - Size];
  }
  (*Count)++;
  return Index;
}

VOID
VecFreeRaw (IN OUT VOID **Items)
{
  FreePool (*Items);
  *Items = NULL;
}

BOOLEAN
BufferReserve (
  IN OUT OABL_BUFFER *Buffer,
  IN UINTN            Extra
)
{
  if (Extra > MAX_UINTN - Buffer->Len) {
    return FALSE;
  }
  return VecGrow ((VOID **)&Buffer->Data, &Buffer->Cap, Buffer->Len + Extra, 1);
}

BOOLEAN
BufferAppend (
  IN OUT OABL_BUFFER *Buffer,
  IN CONST VOID      *Data,
  IN UINTN            Length
)
{
  CONST UINT8 *Source = Data;
  if (!BufferReserve (Buffer, Length)) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Length; Index++) {
    Buffer->Data[Buffer->Len++] = Source[Index];
  }
  return TRUE;
}

BOOLEAN
BufferAppendU8 (
  IN OUT OABL_BUFFER *Buffer,
  IN UINT8            Value
)
{
  return BufferAppend (Buffer, &Value, 1);
}

BOOLEAN
BufferAppendLe16 (
  IN OUT OABL_BUFFER *Buffer,
  IN UINT16           Value
)
{
  UINT8 Bytes[2];
  WriteLe16 (Bytes, Value);
  return BufferAppend (Buffer, Bytes, sizeof (Bytes));
}

BOOLEAN
BufferAppendLe32 (
  IN OUT OABL_BUFFER *Buffer,
  IN UINT32           Value
)
{
  UINT8 Bytes[4];
  WriteLe32 (Bytes, Value);
  return BufferAppend (Buffer, Bytes, sizeof (Bytes));
}

BOOLEAN
BufferAppendLe64 (
  IN OUT OABL_BUFFER *Buffer,
  IN UINT64           Value
)
{
  UINT8 Bytes[8];
  WriteLe64 (Bytes, Value);
  return BufferAppend (Buffer, Bytes, sizeof (Bytes));
}

BOOLEAN
BufferAppendBe32 (
  IN OUT OABL_BUFFER *Buffer,
  IN UINT32           Value
)
{
  UINT8 Bytes[4];
  WriteBe32 (Bytes, Value);
  return BufferAppend (Buffer, Bytes, sizeof (Bytes));
}

BOOLEAN
BufferAppendBe64 (
  IN OUT OABL_BUFFER *Buffer,
  IN UINT64           Value
)
{
  UINT8 Bytes[8];
  WriteBe32 (Bytes, (UINT32)(Value >> 32));
  WriteBe32 (Bytes + 4, (UINT32)Value);
  return BufferAppend (Buffer, Bytes, sizeof (Bytes));
}

BOOLEAN
BufferAppendZeros (
  IN OUT OABL_BUFFER *Buffer,
  IN UINTN            Length
)
{
  if (!BufferReserve (Buffer, Length)) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Length; Index++) {
    Buffer->Data[Buffer->Len++] = 0;
  }
  return TRUE;
}

BOOLEAN
BufferFrom (
  OUT OABL_BUFFER *Buffer,
  IN CONST VOID   *Data,
  IN UINTN         Length
)
{
  *Buffer = (OABL_BUFFER){0};
  // Allocate even for an empty copy, so Data is not NULL.
  if (!BufferReserve (Buffer, Length ? Length : 1)) {
    return FALSE;
  }
  return BufferAppend (Buffer, Data, Length);
}

VOID
BufferFree (IN OUT OABL_BUFFER *Buffer)
{
  FreePool (Buffer->Data);
  *Buffer = (OABL_BUFFER){0};
}

UINT8 *
BufferTake (
  IN OUT OABL_BUFFER *Buffer,
  OUT UINTN          *Length OPTIONAL
)
{
  UINT8 *Data = Buffer->Data;
  if (Length) {
    *Length = Buffer->Len;
  }
  *Buffer = (OABL_BUFFER){0};
  return Data;
}

BOOLEAN
GetLe16 (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  IN UINTN        At,
  OUT UINT16     *Value
)
{
  if (!InBounds (Size, At, 2)) {
    return FALSE;
  }
  *Value = ReadLe16 (Bytes + At);
  return TRUE;
}

BOOLEAN
GetLe32 (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  IN UINTN        At,
  OUT UINT32     *Value
)
{
  if (!InBounds (Size, At, 4)) {
    return FALSE;
  }
  *Value = ReadLe32 (Bytes + At);
  return TRUE;
}

BOOLEAN
GetLe64 (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  IN UINTN        At,
  OUT UINT64     *Value
)
{
  if (!InBounds (Size, At, 8)) {
    return FALSE;
  }
  *Value = ReadLe64 (Bytes + At);
  return TRUE;
}

BOOLEAN
GetBe32 (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  IN UINTN        At,
  OUT UINT32     *Value
)
{
  if (!InBounds (Size, At, 4)) {
    return FALSE;
  }
  *Value = ReadBe32 (Bytes + At);
  return TRUE;
}

BOOLEAN
GetBe64 (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  IN UINTN        At,
  OUT UINT64     *Value
)
{
  if (!InBounds (Size, At, 8)) {
    return FALSE;
  }
  *Value = ReadBe64 (Bytes + At);
  return TRUE;
}

STATIC VOID
Swap (
  IN OUT UINT8 *A,
  IN OUT UINT8 *B,
  IN UINTN      Size
)
{
  for (UINTN Index = 0; Index < Size; Index++) {
    UINT8 Byte = A[Index];
    A[Index]   = B[Index];
    B[Index]   = Byte;
  }
}

VOID
SortBy (
  IN OUT VOID *Elements,
  IN UINTN     Count,
  IN UINTN     Size,
  IN BOOLEAN (*Less) (
    IN CONST VOID *A,
    IN CONST VOID *B,
    IN VOID       *Context
  ),
  IN VOID *Context OPTIONAL
)
{
  UINT8 *Bytes = Elements;

  for (UINTN Index = 1; Index < Count; Index++) {
    for (UINTN Index2 = Index;
         Index2 > 0 &&
         Less (Bytes + Index2 * Size, Bytes + (Index2 - 1) * Size, Context);
         Index2--) {
      Swap (Bytes + Index2 * Size, Bytes + (Index2 - 1) * Size, Size);
    }
  }
}
