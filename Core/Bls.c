/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Bls - UAPI Boot Loader Specification.
*/

#include "Core/Bls.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Buffer.h"

STATIC INT32
Cmp (
  IN UINT64 A,
  IN UINT64 B
)
{
  return (A > B) - (A < B);
}

// Compares strings as unsigned bytes. A prefix sorts first.
STATIC INT32
CmpStr (
  IN CONST CHAR8 *A,
  IN CONST CHAR8 *B
)
{
  CONST UINT8 *BytesA = (CONST UINT8 *)A, *BytesB = (CONST UINT8 *)B;

  while (*BytesA && *BytesA == *BytesB) {
    BytesA++;
    BytesB++;
  }
  return Cmp (*BytesA, *BytesB);
}

STATIC INT32
CmpN (
  IN CONST UINT8 *A,
  IN CONST UINT8 *B,
  IN UINTN        Size
)
{
  for (UINTN Index = 0; Index < Size; Index++) {
    if (A[Index] != B[Index]) {
      return Cmp (A[Index], B[Index]);
    }
  }
  return 0;
}

STATIC BOOLEAN
IsDigit (IN UINT8 C)
{
  return C >= '0' && C <= '9';
}

STATIC BOOLEAN
IsAlpha (IN UINT8 C)
{
  return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z');
}

BLS_COUNTER
BlsCounterBumped (IN BLS_COUNTER Counter)
{
  return (BLS_COUNTER){
    .Left = Counter.Left ? Counter.Left - 1 : 0,
    .Done = Counter.Done >= 0x7fffffff ? 0x7fffffff : Counter.Done + 1,
  };
}

VOID
BlsCounterDebug (
  IN BOOLEAN     Some,
  IN BLS_COUNTER Counter,
  OUT CHAR8     *Buffer,
  IN UINTN       BufferSize
)
{
  if (Some) {
    SPrint (
      Buffer, BufferSize, "Some(Counter { left: %u, done: %u })",
      (UINT32)Counter.Left, (UINT32)Counter.Done
    );
  } else {
    AsciiStrCopy (Buffer, BufferSize, "None");
  }
}

// Parses a number of ASCII digits that fits in 32 bits.
STATIC BOOLEAN
Digits (
  IN OABL_TEXT Text,
  OUT UINT32  *Out
)
{
  UINT64 Value;

  for (UINTN Index = 0; Index < Text.Len; Index++) {
    if (!IsDigit ((UINT8)Text.Ptr[Index])) {
      return FALSE;
    }
  }
  if (!TextParseU64 (Text, 10, &Value) || Value > MAX_UINT32) {
    return FALSE;
  }
  *Out = (UINT32)Value;
  return TRUE;
}

CHAR8 *
BlsSplitCounter (
  IN CONST CHAR8  *File,
  IN CONST CHAR8  *Suffix,
  OUT BOOLEAN     *Counted,
  OUT BLS_COUNTER *Counter
)
{
  OABL_TEXT Name = TextOf (File);
  OABL_TEXT Stem;
  OABL_TEXT Ext;
  OABL_TEXT Base;
  OABL_TEXT CounterText;
  OABL_TEXT LeftText;
  OABL_TEXT DoneText;
  UINTN     SuffixLength = AsciiStrLen (Suffix);
  UINTN     Plus;
  UINT32    Left = 0;
  UINT32    Done = 0;
  BOOLEAN   Ok;
  CHAR8    *Id;

  *Counted = FALSE;
  if (Name.Len < SuffixLength ||
      !TextEqNoCase (
        TextSub (Name, Name.Len - SuffixLength, Name.Len), Suffix
      )) {
    return AsciiStrDup (File);
  }
  Stem = TextSub (Name, 0, Name.Len - SuffixLength);
  Ext  = TextSub (Name, Name.Len - SuffixLength, Name.Len);
  if (!TextFindLastChar (Stem, '+', &Plus)) {
    return AsciiStrDup (File);
  }
  Base        = TextSub (Stem, 0, Plus);
  CounterText = TextSub (Stem, Plus + 1, Stem.Len);
  if (TextSplitOnce (CounterText, '-', &LeftText, &DoneText)) {
    Ok = Digits (LeftText, &Left) && Digits (DoneText, &Done);
  } else {
    Ok = Digits (CounterText, &Left);
  }
  if (!Ok || !Base.Len) {
    return AsciiStrDup (File);
  }
  // Ext is the end of File, so it is NUL-terminated and keeps its case.
  Id = SPrintAlloc ("%.*s%s", (INT32)Base.Len, Base.Ptr, Ext.Ptr);
  if (Id) {
    *Counted = TRUE;
    *Counter = (BLS_COUNTER){Left, Done};
  }
  return Id;
}

CHAR8 *
BlsCountedName (
  IN CONST CHAR8 *Id,
  IN BLS_COUNTER  Counter
)
{
  UINTN Dot;

  if (!TextFindLastChar (TextOf (Id), '.', &Dot)) {
    Dot = AsciiStrLen (Id);
  }
  return SPrintAlloc (
    "%.*s+%u-%u%s", (INT32)Dot, Id, (UINT32)Counter.Left, (UINT32)Counter.Done,
    Id + Dot
  );
}

STATIC BOOLEAN
ValidVersionChar (IN UINT8 C)
{
  return IsDigit (C) || IsAlpha (C) || C == '~' || C == '-' || C == '^' ||
         C == '.';
}

STATIC BOOLEAN
Starts (
  IN CONST UINT8 *Bytes,
  IN UINTN        Length,
  IN UINT8        C
)
{
  return Length && *Bytes == C;
}

STATIC UINTN
Span (
  IN CONST UINT8 *Bytes,
  IN UINTN        Length,
  IN BOOLEAN (*Is) (IN UINT8 C)
)
{
  UINTN Count = 0;
  while (Count < Length && Is (Bytes[Count])) {
    Count++;
  }
  return Count;
}

INT32
BlsVerCmp (
  IN CONST CHAR8 *VersionA,
  IN CONST CHAR8 *VersionB
)
{
  STATIC CONST UINT8 Separators[] = {'-', '^', '.'};
  CONST UINT8       *A = (CONST UINT8 *)VersionA, *B = (CONST UINT8 *)VersionB;
  UINTN              LengthA = AsciiStrLen (VersionA);
  UINTN              LengthB = AsciiStrLen (VersionB);

  for (;;) {
    UINTN   SpanA;
    UINTN   SpanB;
    BOOLEAN DigitA;
    BOOLEAN DigitB;
    INT32   Order;

    while (LengthA && !ValidVersionChar (*A)) {
      A++;
      LengthA--;
    }
    while (LengthB && !ValidVersionChar (*B)) {
      B++;
      LengthB--;
    }
    if (Starts (A, LengthA, '~') || Starts (B, LengthB, '~')) {
      // The pre-release (with '~') is older.
      Order = Cmp (!Starts (A, LengthA, '~'), !Starts (B, LengthB, '~'));
      if (Order) {
        return Order;
      }
      A++, LengthA--;
      B++, LengthB--;
    }
    if (!LengthA || !LengthB) {
      return Cmp (LengthA != 0, LengthB != 0);
    }
    for (UINTN Index = 0; Index < sizeof (Separators); Index++) {
      if (Starts (A, LengthA, Separators[Index]) ||
          Starts (B, LengthB, Separators[Index])) {
        Order = Cmp (
          !Starts (A, LengthA, Separators[Index]),
          !Starts (B, LengthB, Separators[Index])
        );
        if (Order) {
          return Order;
        }
        A++, LengthA--;
        B++, LengthB--;
      }
    }
    DigitA = LengthA && IsDigit (*A);
    DigitB = LengthB && IsDigit (*B);
    if (DigitA || DigitB) {
      UINTN ZerosA;
      UINTN ZerosB;

      // Numbers are newer than letters.
      Order = Cmp (DigitA, DigitB);
      if (Order) {
        return Order;
      }
      SpanA = Span (A, LengthA, IsDigit);
      SpanB = Span (B, LengthB, IsDigit);
      // Leading zeros don't count.
      for (ZerosA = 0; ZerosA < SpanA && A[ZerosA] == '0'; ZerosA++)
        ;
      for (ZerosB = 0; ZerosB < SpanB && B[ZerosB] == '0'; ZerosB++)
        ;
      Order = Cmp (SpanA - ZerosA, SpanB - ZerosB);
      if (!Order) {
        Order = CmpN (A + ZerosA, B + ZerosB, SpanA - ZerosA);
      }
      if (Order) {
        return Order;
      }
    } else {
      SpanA = Span (A, LengthA, IsAlpha);
      SpanB = Span (B, LengthB, IsAlpha);
      Order = CmpN (A, B, SpanA < SpanB ? SpanA : SpanB);
      if (!Order) {
        Order = Cmp (SpanA, SpanB);
      }
      if (Order) {
        return Order;
      }
    }
    A += SpanA, LengthA -= SpanA;
    B += SpanB, LengthB -= SpanB;
  }
}

// Matches C against the [...] class at the start of Pattern and returns its
// length in ClassLength. Fails if the class isn't closed.
STATIC BOOLEAN
GlobClass (
  IN CONST UINT8 *Pattern,
  IN UINTN        Length,
  IN UINT8        C,
  OUT BOOLEAN    *Hit,
  OUT UINTN      *ClassLength
)
{
  UINTN   Index = 1;
  BOOLEAN Negate =
    Index < Length && (Pattern[Index] == '!' || Pattern[Index] == '^');
  BOOLEAN Matched = FALSE;
  BOOLEAN First   = TRUE;

  if (Negate) {
    Index++;
  }
  while (Index < Length) {
    UINT8 Member = Pattern[Index];

    if (Member == ']' && !First) {
      *Hit         = Matched != Negate;
      *ClassLength = Index + 1;
      return TRUE;
    }
    First = FALSE;
    if (Index + 2 < Length && Pattern[Index + 1] == '-' &&
        Pattern[Index + 2] != ']') {
      Matched |= Member <= C && C <= Pattern[Index + 2];
      Index += 3;
    } else {
      Matched |= Member == C;
      Index++;
    }
  }
  return FALSE;
}

STATIC BOOLEAN
GlobGo (
  IN CONST UINT8 *Pattern,
  IN UINTN        PatternLength,
  IN CONST UINT8 *Name,
  IN UINTN        NameLength
)
{
  BOOLEAN Hit;
  UINTN   ClassLength;

  if (!PatternLength) {
    return !NameLength;
  }
  switch (*Pattern) {
  case '*':
    for (UINTN Skip = 0; Skip <= NameLength; Skip++) {
      if (GlobGo (
            Pattern + 1, PatternLength - 1, Name + Skip, NameLength - Skip
          )) {
        return TRUE;
      }
    }
    return FALSE;
  case '?':
    return NameLength &&
           GlobGo (Pattern + 1, PatternLength - 1, Name + 1, NameLength - 1);
  case '[':
    if (!NameLength) {
      return FALSE;
    }
    if (GlobClass (Pattern, PatternLength, *Name, &Hit, &ClassLength)) {
      return Hit && GlobGo (
                      Pattern + ClassLength, PatternLength - ClassLength,
                      Name + 1, NameLength - 1
                    );
    }
    // An unterminated class matches a literal '['.
    return *Name == '[' &&
           GlobGo (Pattern + 1, PatternLength - 1, Name + 1, NameLength - 1);
  default:
    return NameLength && *Name == *Pattern &&
           GlobGo (Pattern + 1, PatternLength - 1, Name + 1, NameLength - 1);
  }
}

BOOLEAN
BlsGlob (
  IN CONST CHAR8 *Pattern,
  IN CONST CHAR8 *Name
)
{
  return GlobGo (
    (CONST UINT8 *)Pattern, AsciiStrLen (Pattern), (CONST UINT8 *)Name,
    AsciiStrLen (Name)
  );
}

STATIC BOOLEAN
Push (
  IN OUT BOOT_ENTRY_STRINGS *List,
  IN OABL_TEXT               Value
)
{
  CHAR8 *Copy = TextDup (Value);

  if (!Copy || !VecPush (List, Copy)) {
    FreePool (Copy);
    return FALSE;
  }
  return TRUE;
}

BOOLEAN
BlsParseType1 (
  IN OABL_TEXT   Text,
  OUT BLS_TYPE1 *Out
)
{
  OABL_TEXT Line;
  OABL_TEXT Key;
  OABL_TEXT Value;

  *Out = (BLS_TYPE1){0};
  while (TextNextLine (&Text, &Line)) {
    BOOLEAN Ok = TRUE;

    Line = TextTrim (Line);
    if (!Line.Len || Line.Ptr[0] == '#') {
      continue;
    }
    BootEntryKeyValue (Line, &Key, &Value);
    if (TextEqStr (Key, "title")) {
      Ok = BootEntrySetText (&Out->Title, Value);
    } else if (TextEqStr (Key, "version")) {
      Ok = BootEntrySetText (&Out->Version, Value);
    } else if (TextEqStr (Key, "machine-id")) {
      Ok = BootEntrySetText (&Out->MachineId, Value);
    } else if (TextEqStr (Key, "sort-key")) {
      Ok = BootEntrySetText (&Out->SortKey, Value);
    } else if (TextEqStr (Key, "linux")) {
      Ok = BootEntrySetText (&Out->Kernel, Value);
    } else if (TextEqStr (Key, "initrd")) {
      Ok = BootEntryPushWords (&Out->Initrd, Value, IsAsciiSpace);
    } else if (TextEqStr (Key, "efi")) {
      Ok = BootEntrySetText (&Out->Efi, Value);
    } else if (TextEqStr (Key, "uki")) {
      Ok = BootEntrySetText (&Out->Uki, Value);
    } else if (TextEqStr (Key, "options") && Value.Len) {
      Ok = Push (&Out->Options, Value);
    } else if (TextEqStr (Key, "devicetree")) {
      Ok = BootEntrySetText (&Out->Devicetree, Value);
    } else if (TextEqStr (Key, "devicetree-overlay")) {
      Ok = BootEntryPushWords (&Out->DevicetreeOverlay, Value, IsAsciiSpace);
    } else if (TextEqStr (Key, "architecture")) {
      Ok = BootEntrySetText (&Out->Architecture, Value);
    }
    if (!Ok) {
      BlsType1Free (Out);
      return FALSE;
    }
  }
  return TRUE;
}

VOID
BlsType1Free (IN OUT BLS_TYPE1 *Type1)
{
  FreePool (Type1->Title);
  FreePool (Type1->Version);
  FreePool (Type1->MachineId);
  FreePool (Type1->SortKey);
  FreePool (Type1->Kernel);
  BootEntryStringsFree (&Type1->Initrd);
  FreePool (Type1->Efi);
  FreePool (Type1->Uki);
  BootEntryStringsFree (&Type1->Options);
  FreePool (Type1->Devicetree);
  BootEntryStringsFree (&Type1->DevicetreeOverlay);
  FreePool (Type1->Architecture);
  *Type1 = (BLS_TYPE1){0};
}

STATIC CHAR8 *
Join (IN CONST BOOT_ENTRY_STRINGS *List)
{
  OABL_STRING Joined = {0};

  for (UINTN Index = 0; Index < List->Count; Index++) {
    if (Index) {
      StringAppendChar (&Joined, ' ');
    }
    StringAppend (&Joined, List->Items[Index]);
  }
  if (Joined.Failed) {
    StringFree (&Joined);
    return NULL;
  }
  return StringTake (&Joined);
}

BOOLEAN
BlsType1Entry (
  IN CONST CHAR8 *FileName,
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Arch,
  OUT BOOT_ENTRY *Out
)
{
  BLS_TYPE1          Type1;
  BOOT_ENTRY         Entry   = {.Source = BOOT_ENTRY_SOURCE_TYPE1};
  BOOT_ENTRY_ACTION *Action  = &Entry.Action;
  CHAR8             *Options = NULL;
  BOOLEAN            Ok;

  *Out = (BOOT_ENTRY){0};
  if (!BlsParseType1 (Text, &Type1)) {
    return FALSE;
  }
  // Skip other architectures and entries with nothing to start. linux wins
  // over uki, and uki over efi.
  if ((Type1.Architecture &&
       !TextEqNoCase (TextOf (Type1.Architecture), Arch)) ||
      (!Type1.Kernel && !Type1.Uki && !Type1.Efi)) {
    BlsType1Free (&Type1);
    return FALSE;
  }
  Entry.Id =
    BlsSplitCounter (FileName, ".conf", &Entry.HasCounter, &Entry.Counter);
  Ok = Entry.Id != NULL;
  if (Type1.Options.Count) {
    Options = Join (&Type1.Options);
    Ok      = Ok && Options;
  }
  if (Type1.Kernel) {
    Action->Kind    = BOOT_ENTRY_ACTION_LINUX;
    Action->Kernel  = BootEntryUefiPath (TextOf (Type1.Kernel));
    Action->Cmdline = Options ? Options : AsciiStrDup ("");
    Options         = NULL;
    Ok = Ok && Action->Kernel && Action->Cmdline &&
         BootEntryUefiPaths (&Type1.Initrd, &Action->Initrds) &&
         BootEntryOptUefiPath (&Action->Devicetree, Type1.Devicetree) &&
         BootEntryUefiPaths (&Type1.DevicetreeOverlay, &Action->Overlays);
  } else {
    Action->Kind = BOOT_ENTRY_ACTION_EFI;
    Action->Path =
      BootEntryUefiPath (TextOf (Type1.Uki ? Type1.Uki : Type1.Efi));
    Action->Options = Options;
    Options         = NULL;
    Ok = Ok && Action->Path &&
         BootEntryOptUefiPath (&Action->Devicetree, Type1.Devicetree);
  }
  if (Type1.Title) {
    Entry.Title = Type1.Title;
    Type1.Title = NULL;
  } else if (Entry.Id) {
    OABL_TEXT Id = TextOf (Entry.Id);
    Entry.Title =
      TextDup (TextEndsWith (Id, ".conf") ? TextSub (Id, 0, Id.Len - 5) : Id);
  }
  Entry.Version   = Type1.Version;
  Entry.SortKey   = Type1.SortKey;
  Entry.MachineId = Type1.MachineId;
  Type1.Version = Type1.SortKey = Type1.MachineId = NULL;
  Entry.File = SPrintAlloc ("%s\\%s", BLS_ENTRIES_DIR, FileName);
  Ok         = Ok && Entry.Title && Entry.File;
  BlsType1Free (&Type1);
  if (!Ok) {
    BootEntryFree (&Entry);
    return FALSE;
  }
  *Out = Entry;
  return TRUE;
}

BOOLEAN
BlsOsRelease (
  IN OABL_TEXT   Text,
  OUT BLS_PAIRS *Out
)
{
  OABL_TEXT Line;
  OABL_TEXT Key;
  OABL_TEXT Value;

  *Out = (BLS_PAIRS){0};
  while (TextNextLine (&Text, &Line)) {
    BLS_PAIR Pair;
    BOOLEAN  Escaped = FALSE;
    UINTN    Length  = 0;

    Line = TextTrim (Line);
    if (Line.Len && Line.Ptr[0] == '#') {
      continue;
    }
    if (!TextSplitOnce (Line, '=', &Key, &Value)) {
      continue;
    }
    Value = TextTrim (Value);
    if (Value.Len >= 2 && (Value.Ptr[0] == '"' || Value.Ptr[0] == '\'') &&
        Value.Ptr[Value.Len - 1] == Value.Ptr[0]) {
      Value = TextSub (Value, 1, Value.Len - 1);
    }
    Pair.Key   = TextDup (TextTrim (Key));
    Pair.Value = AllocatePool (Value.Len + 1);
    if (!Pair.Key || !Pair.Value || !VecPush (Out, Pair)) {
      FreePool (Pair.Key);
      FreePool (Pair.Value);
      BlsPairsFree (Out);
      return FALSE;
    }
    // A backslash escapes the next character and is dropped.
    for (UINTN Index = 0; Index < Value.Len; Index++) {
      if (Escaped || Value.Ptr[Index] != '\\') {
        Pair.Value[Length++] = Value.Ptr[Index];
        Escaped              = FALSE;
      } else {
        Escaped = TRUE;
      }
    }
    Pair.Value[Length] = 0;
  }
  return TRUE;
}

VOID
BlsPairsFree (IN OUT BLS_PAIRS *Pairs)
{
  for (UINTN Index = 0; Index < Pairs->Count; Index++) {
    FreePool (Pairs->Items[Index].Key);
    FreePool (Pairs->Items[Index].Value);
  }
  VecFree (Pairs);
}

// Returns the first non-empty value among Keys (NULL-terminated).
STATIC CONST CHAR8 *
Get (
  IN CONST BLS_PAIRS    *Pairs,
  IN CONST CHAR8 *CONST *Keys
)
{
  for (; *Keys; Keys++) {
    for (UINTN Index = 0; Index < Pairs->Count; Index++) {
      if (AsciiStrEq (Pairs->Items[Index].Key, *Keys) &&
          Pairs->Items[Index].Value[0]) {
        return Pairs->Items[Index].Value;
      }
    }
  }
  return NULL;
}

BOOLEAN
BlsUkiEntry (
  IN CONST CHAR8 *FileName,
  IN OABL_TEXT    Osrel,
  IN OABL_TEXT    Uname,
  OUT BOOT_ENTRY *Out
)
{
  STATIC CONST CHAR8 *CONST TitleKeys[]   = {"PRETTY_NAME", "NAME", NULL};
  STATIC CONST CHAR8 *CONST VersionKeys[] = {"IMAGE_VERSION", "VERSION_ID",
                                             "BUILD_ID", NULL};
  STATIC CONST CHAR8 *CONST SortKeys[]    = {"IMAGE_ID", "ID", NULL};
  BLS_PAIRS                 Pairs;
  BOOT_ENTRY                Entry = {.Source      = BOOT_ENTRY_SOURCE_UKI,
                                     .Action.Kind = BOOT_ENTRY_ACTION_EFI};
  CONST CHAR8              *Value;
  BOOLEAN                   Ok = TRUE;

  *Out = (BOOT_ENTRY){0};
  if (!BlsOsRelease (Osrel, &Pairs)) {
    return FALSE;
  }
  Entry.Id =
    BlsSplitCounter (FileName, ".efi", &Entry.HasCounter, &Entry.Counter);
  Value       = Get (&Pairs, TitleKeys);
  Entry.Title = AsciiStrDup (Value ? Value : "Linux");
  Value       = Get (&Pairs, VersionKeys);
  if (Value) {
    Entry.Version = AsciiStrDup (Value);
    Ok            = Entry.Version != NULL;
  } else {
    Uname = TextTrim (Uname);
    if (Uname.Len) {
      Entry.Version = TextDup (Uname);
      Ok            = Entry.Version != NULL;
    }
  }
  Value = Get (&Pairs, SortKeys);
  if (Value) {
    Entry.SortKey = AsciiStrDup (Value);
    Ok            = Ok && Entry.SortKey;
  }
  Entry.File        = SPrintAlloc ("%s\\%s", BLS_UKI_DIR, FileName);
  Entry.Action.Path = Entry.File ? AsciiStrDup (Entry.File) : NULL;
  Ok                = Ok && Entry.Id && Entry.Title && Entry.Action.Path;
  BlsPairsFree (&Pairs);
  if (!Ok) {
    BootEntryFree (&Entry);
    return FALSE;
  }
  *Out = Entry;
  return TRUE;
}

// Finds the last "Key value" line of loader.conf with a value.
STATIC BOOLEAN
ConfValue (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Key,
  OUT OABL_TEXT  *Out
)
{
  OABL_TEXT Line;
  OABL_TEXT Value;
  UINTN     KeyLen = AsciiStrLen (Key);
  BOOLEAN   Found  = FALSE;

  while (TextNextLine (&Text, &Line)) {
    Line = TextTrim (Line);
    if (!TextStartsWith (Line, Key)) {
      continue;
    }
    Value = TextSub (Line, KeyLen, Line.Len);
    if (!Value.Len || !IsAsciiSpace (Value.Ptr[0])) {
      continue;
    }
    Value = TextTrim (Value);
    if (Value.Len) {
      *Out  = Value;
      Found = TRUE;
    }
  }
  return Found;
}

CHAR8 *
BlsLoaderConfDefault (IN OABL_TEXT Text)
{
  OABL_TEXT Default;

  return ConfValue (Text, "default", &Default) ? TextDup (Default) : NULL;
}

BOOLEAN
BlsParseTimeout (
  IN OABL_TEXT  Value,
  OUT BLS_MENU *Menu,
  OUT UINT32   *Seconds
)
{
  UINT64 Number;

  if (TextEqStr (Value, "menu-force")) {
    *Menu    = BLS_MENU_FORCE;
    *Seconds = 0;
    return TRUE;
  }
  if (TextEqStr (Value, "menu-hidden") || TextEqStr (Value, "menu-disabled")) {
    *Menu    = BLS_MENU_HIDDEN;
    *Seconds = 0;
    return TRUE;
  }
  if (!TextParseU64 (Value, 10, &Number) || Number > MAX_UINT32) {
    return FALSE;
  }
  *Menu    = Number ? BLS_MENU_TIMEOUT : BLS_MENU_HIDDEN;
  *Seconds = (UINT32)Number;
  return TRUE;
}

BOOLEAN
BlsLoaderConfTimeout (
  IN OABL_TEXT  Text,
  OUT BLS_MENU *Menu,
  OUT UINT32   *Seconds
)
{
  OABL_TEXT Value;

  return ConfValue (Text, "timeout", &Value) &&
         BlsParseTimeout (Value, Menu, Seconds);
}

// Compares strings that may be NULL. NULL sorts first.
STATIC INT32
CmpOpt (
  IN CONST CHAR8 *A OPTIONAL,
  IN CONST CHAR8 *B OPTIONAL
)
{
  if (!A || !B) {
    return Cmp (A != NULL, B != NULL);
  }
  return CmpStr (A, B);
}

INT32
BlsCompare (
  IN CONST BOOT_ENTRY *A,
  IN CONST BOOT_ENTRY *B
)
{
  INT32 Order = Cmp (BootEntryIsBad (A), BootEntryIsBad (B));

  if (Order) {
    return Order;
  }
  Order = Cmp (!A->SortKey, !B->SortKey);
  if (Order) {
    return Order;
  }
  if (A->SortKey && B->SortKey) {
    Order = CmpStr (A->SortKey, B->SortKey);
    if (!Order) {
      Order = CmpOpt (A->MachineId, B->MachineId);
    }
    if (!Order) {
      Order =
        BlsVerCmp (B->Version ? B->Version : "", A->Version ? A->Version : "");
    }
    if (Order) {
      return Order;
    }
  }
  Order = BlsVerCmp (B->Id, A->Id);
  if (Order) {
    return Order;
  }
  if (A->HasCounter && B->HasCounter) {
    Order = Cmp (B->Counter.Left, A->Counter.Left);
    return Order ? Order : Cmp (A->Counter.Done, B->Counter.Done);
  }
  return 0;
}

STATIC BOOLEAN
Less (
  IN CONST VOID *A,
  IN CONST VOID *B,
  IN VOID       *Context
)
{
  (VOID)Context;
  return BlsCompare (A, B) < 0;
}

VOID
BlsSort (
  IN OUT BOOT_ENTRY *Entries,
  IN UINTN           Count
)
{
  SortBy (Entries, Count, sizeof (*Entries), Less, NULL);
}

BOOLEAN
BlsPickDefault (
  IN CONST BOOT_ENTRY *Entries,
  IN UINTN             Count,
  IN CONST CHAR8      *Pattern OPTIONAL,
  OUT UINTN           *Out
)
{
  if (!Count) {
    return FALSE;
  }
  if (Pattern) {
    for (UINTN Index = 0; Index < Count; Index++) {
      if (!BootEntryIsBad (&Entries[Index]) &&
          BlsGlob (Pattern, Entries[Index].Id)) {
        *Out = Index;
        return TRUE;
      }
    }
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    if (!BootEntryIsBad (&Entries[Index])) {
      *Out = Index;
      return TRUE;
    }
  }
  *Out = 0;
  return TRUE;
}
