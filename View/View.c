/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  View - the menu's screens on a text grid (View/Grid.h), laid out the way
  portarelauncher lays out its menus.

  A screen has the header over a double rule at the top, the key hints
  under a rule at the bottom, and its rows between. Nothing moves, so a
  frame is made only when the app changes, and only the cells that changed
  go to the panel, rotated.
*/

#include "View/View.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Ui/Lang.h"
#include "View/Grid.h"

// The name in the header.
#define PRODUCT "BIOS"

// Columns: the caret, then labels, notes and wrapped text. Values end
// MARGIN columns before the right edge.
#define CARET_X 2
#define TEXT_X  4
#define MARGIN  2
// The first row under the header's rule, after a blank one.
#define BODY_Y 3

// CP437's arrows, shown when a list has more above or below.
#define GLYPH_MORE_UP   0x18
#define GLYPH_MORE_DOWN 0x19

STATIC struct {
  BOOLEAN Ready;
  PANEL   Panel;
  UINT16  Rotation;
  GRID    Grid;
  // A rotated area on its way to the panel.
  UINT32 *Scratch;
  // The grid has changes the panel doesn't show yet.
  BOOLEAN Pending;
} mView;

// Rows

// Copies String to Out in capitals, for a section's label.
STATIC VOID
Upper (
  IN CONST CHAR8 *String,
  OUT CHAR8      *Out,
  IN UINTN        Cap
)
{
  UINTN Length = 0;

  while (String[Length] && Length + 1 < Cap) {
    CHAR8 C     = String[Length];
    Out[Length] = C >= 'a' && C <= 'z' ? C - 'a' + 'A' : C;
    Length++;
  }
  Out[Length] = 0;
}

// Writes Text from column X, cut short at a space to Width columns.
STATIC VOID
PutsCut (
  IN UINT32       X,
  IN UINT32       Y,
  IN UINT32       Width,
  IN CONST CHAR8 *Text,
  IN ATTR         Attr
)
{
  GridWrap (&mView.Grid, X, Y, Width, 1, Text, Attr);
}

// A row with Label at TEXT_X and Value ending on column End - 1, and the
// caret when Focused. The focused row is bright. A value that doesn't fit
// beside the label is cut short.
STATIC VOID
RowDraw (
  IN UINT32       Y,
  IN BOOLEAN      Focused,
  IN CONST CHAR8 *Label,
  IN CONST CHAR8 *Value OPTIONAL,
  IN UINT32       End
)
{
  GRID  *G    = &mView.Grid;
  ATTR   Attr = Focused ? ATTR_BRIGHT : ATTR_TEXT;
  UINT32 Room = End > TEXT_X ? End - TEXT_X : 0;
  UINT32 Used = GridWidth (Label);

  if (Focused) {
    GridPutc (G, CARET_X, Y, GLYPH_CARET, Attr);
  }
  PutsCut (TEXT_X, Y, Room, Label, Attr);
  if (!Value || !Value[0]) {
    return;
  }
  Used = TEXT_X + (Used < Room ? Used : Room);
  if (Used + 1 + GridWidth (Value) <= End) {
    GridPutsRight (G, End, Y, Value, Attr);
  } else if (Used + 2 < End) {
    PutsCut (Used + 2, Y, End - Used - 2, Value, Attr);
  }
}

// Marks a list that has more rows above its first row Top or below its
// last row Bottom - 1.
STATIC VOID
MoreMarks (
  IN UINT32  Top,
  IN UINT32  Bottom,
  IN BOOLEAN Above,
  IN BOOLEAN Below
)
{
  GRID *G = &mView.Grid;

  if (Above) {
    GridPutc (G, G->Cols - MARGIN, Top, GLYPH_MORE_UP, ATTR_MID);
  }
  if (Below) {
    GridPutc (G, G->Cols - MARGIN, Bottom - 1, GLYPH_MORE_DOWN, ATTR_MID);
  }
}

// Header and footer

// The product, then Crumb if any, and the version at the right, over a
// double rule.
STATIC VOID
HeaderDraw (
  IN CONST APP   *A,
  IN CONST CHAR8 *Crumb OPTIONAL
)
{
  GRID  *G = &mView.Grid;
  CHAR8  Title[128];
  CHAR8  Version[32] = "";
  UINT32 Room;

  AsciiStrCopy (Title, sizeof (Title), PRODUCT);
  if (Crumb) {
    AsciiStrAppend (Title, sizeof (Title), "  >  ");
    AsciiStrAppend (Title, sizeof (Title), Crumb);
  }
  if (A->Version && A->Version[0]) {
    AsciiStrCopy (Version, sizeof (Version), "v");
    AsciiStrAppend (Version, sizeof (Version), A->Version);
  }
  // A margin on each side and a space before the version.
  Room = G->Cols > GridWidth (Version) + 3 ? G->Cols - GridWidth (Version) - 3
                                           : 0;
  PutsCut (1, 0, Room, Title, ATTR_TEXT);
  GridPutsRight (G, G->Cols - 1, 0, Version, ATTR_TEXT);
  GridRule (G, 1, GLYPH_RULE_DOUBLE, ATTR_MID);
}

// The key hints on the last row, under a rule: the key that acts and Verb
// at the left, and with Move the keys that move at the right.
STATIC VOID
FooterDraw (
  IN CONST APP   *A,
  IN CONST CHAR8 *Verb,
  IN BOOLEAN      Move
)
{
  GRID   *G        = &mView.Grid;
  BOOLEAN Handheld = A->Scheme == SCHEME_HANDHELD;
  CHAR8   Hint[64];

  GridRule (G, G->Rows - 2, GLYPH_RULE, ATTR_MID);
  AsciiStrCopy (Hint, sizeof (Hint), Handheld ? gStrings->KeyPower
                                              : gStrings->KeyEnter);
  AsciiStrAppend (Hint, sizeof (Hint), " ");
  AsciiStrAppend (Hint, sizeof (Hint), Verb);
  GridPuts (G, 1, G->Rows - 1, Hint, ATTR_TEXT);
  if (Move) {
    AsciiStrCopy (Hint, sizeof (Hint), Handheld ? gStrings->KeyVolume
                                                : gStrings->KeyArrows);
    AsciiStrAppend (Hint, sizeof (Hint), " ");
    AsciiStrAppend (Hint, sizeof (Hint), gStrings->VerbMove);
    GridPutsRight (G, G->Cols - 1, G->Rows - 1, Hint, ATTR_TEXT);
  }
}

// Menu

// A row of the menu as the grid shows it. A section after the first gets
// a rule above its label.
typedef enum { LINE_RULE, LINE_LABEL, LINE_ITEM } LINE_KIND;

typedef struct _LINE {
  LINE_KIND Kind;
  UINTN     Row; // the app's row
} LINE;

// Returns the first line to show so line Focus is in view, moving as
// little as possible from Scroll. A section's label stays with the first
// item under it, and the list doesn't start on a rule.
STATIC UINTN
MenuPlace (
  IN CONST LINE *Lines,
  IN UINTN       Count,
  IN UINTN       Focus,
  IN UINTN       Height,
  IN UINTN       Scroll
)
{
  if (Count <= Height) {
    return 0;
  }
  if (Focus < Scroll) {
    Scroll = Focus;
    while (Scroll > 0 && Lines[Scroll - 1].Kind != LINE_ITEM) {
      Scroll--;
    }
  } else if (Focus >= Scroll + Height) {
    Scroll = Focus + 1 - Height;
  }
  if (Scroll + Height > Count) {
    Scroll = Count - Height;
  }
  if (Lines[Scroll].Kind == LINE_RULE && Scroll < Focus) {
    Scroll++;
  }
  return Scroll;
}

// What starting Target starts, by name.
STATIC CONST CHAR8 *
TargetName (
  IN CONST APP   *A,
  IN BOOT_TARGET  Target
)
{
  if (Target == BOOT_TARGET_ORDER) {
    Target = A->Settings.BootMode == BOOT_MODE_ANDROID ? BOOT_TARGET_ANDROID
                                                       : BOOT_TARGET_PORTAREOS;
  }
  return Target == BOOT_TARGET_ANDROID ? gStrings->Android
                                       : gStrings->PortareOs;
}

// The sections and their items, and under a rule a note: the countdown, a
// toast, or what the focused item does.
STATIC VOID
MenuDraw (IN OUT APP *A)
{
  GRID        *G = &mView.Grid;
  ROW          Rows[MAX_ROWS];
  LINE         Lines[MAX_ROWS * 2];
  ITEM_VIEW    Focused;
  CHAR8        Note[160];
  CONST CHAR8 *NoteText;
  UINT64       Seconds;
  UINTN        Count     = AppRows (A, Rows);
  UINTN        Focus     = AppFocusedRow (A, Rows, Count);
  UINTN        LineCount = 0;
  UINTN        FocusLine = 0;
  UINT32       Bottom;
  UINTN        Height;

  for (UINTN Index = 0; Index < Count; Index++) {
    if (Rows[Index].Header) {
      if (LineCount) {
        Lines[LineCount++] = (LINE){LINE_RULE, Index};
      }
      Lines[LineCount++] = (LINE){LINE_LABEL, Index};
      continue;
    }
    if (Index == Focus) {
      FocusLine = LineCount;
    }
    Lines[LineCount++] = (LINE){LINE_ITEM, Index};
  }

  AppItemView (A, Rows[Focus].Item, &Focused);
  NoteText = Focused.Help;
  if (A->ToastUntil) {
    NoteText = A->Toast;
  }
  if (AppCountdownSeconds (A, &Seconds)) {
    CHAR8        Number[8];
    CONST CHAR8 *Args[2] = {TargetName (A, BOOT_TARGET_ORDER), Number};

    AsciiStrFromU64 (Number, sizeof (Number), Seconds);
    LangFill (Note, sizeof (Note), gStrings->Countdown, Args, 2);
    AsciiStrAppend (Note, sizeof (Note), ". ");
    AsciiStrAppend (Note, sizeof (Note), gStrings->AnyKeyToStay);
    AsciiStrAppend (Note, sizeof (Note), ".");
    NoteText = Note;
  }

  HeaderDraw (A, NULL);
  // The list ends above the footer's rule, or above the note and its rule.
  Bottom = G->Rows - 2 - (NoteText ? 2 : 0);
  Height = Bottom > BODY_Y ? Bottom - BODY_Y : 1;
  A->Menu.Scroll =
    MenuPlace (Lines, LineCount, FocusLine, Height, A->Menu.Scroll);
  for (UINTN Line = A->Menu.Scroll;
       Line < LineCount && Line < A->Menu.Scroll + Height; Line++) {
    UINT32     Y   = BODY_Y + (UINT32)(Line - A->Menu.Scroll);
    CONST ROW *Row = &Rows[Lines[Line].Row];
    CHAR8      Label[48];
    ITEM_VIEW  View;

    switch (Lines[Line].Kind) {
    case LINE_RULE:
      GridRule (G, Y, GLYPH_RULE, ATTR_MID);
      break;
    case LINE_LABEL:
      Upper (Row->Header, Label, sizeof (Label));
      GridPuts (G, CARET_X, Y, Label, ATTR_TEXT);
      break;
    case LINE_ITEM:
      AppItemView (A, Row->Item, &View);
      RowDraw (
        Y, Lines[Line].Row == Focus, View.Label, View.Value, G->Cols - MARGIN
      );
      break;
    }
  }
  MoreMarks (
    BODY_Y, BODY_Y + (UINT32)Height, A->Menu.Scroll > 0,
    A->Menu.Scroll + Height < LineCount
  );
  if (NoteText) {
    GridRule (G, G->Rows - 4, GLYPH_RULE, ATTR_MID);
    PutsCut (
      TEXT_X, G->Rows - 3, G->Cols - TEXT_X - MARGIN, NoteText, ATTR_TEXT
    );
  }
  FooterDraw (A, Focused.Verb, TRUE);
}

// Lists: device models, boot entries, and the device info or the log

// Gets list row Index's label, and its value in Value (Cap bytes).
typedef VOID (*LIST_ROW) (
  IN CONST APP     *A,
  IN UINTN          Index,
  OUT CONST CHAR8 **Label,
  OUT CHAR8        *Value,
  IN UINTN          Cap
);

// Count rows from Row under Crumb, scrolled so the focused one shows.
STATIC VOID
ListDraw (
  IN CONST APP      *A,
  IN CONST CHAR8    *Crumb,
  IN UINTN           Count,
  IN OUT LIST_STATE *List,
  IN LIST_ROW        Row,
  IN CONST CHAR8    *Verb
)
{
  GRID  *G      = &mView.Grid;
  UINT32 Bottom = G->Rows - 2;
  UINTN  Height = Bottom > BODY_Y ? Bottom - BODY_Y : 1;

  if (Count <= Height) {
    List->Scroll = 0;
  } else if (List->Focus < List->Scroll) {
    List->Scroll = List->Focus;
  } else if (List->Focus >= List->Scroll + Height) {
    List->Scroll = List->Focus + 1 - Height;
  }
  if (List->Scroll + Height > Count && Count > Height) {
    List->Scroll = Count - Height;
  }

  HeaderDraw (A, Crumb);
  for (UINTN Index = List->Scroll;
       Index < Count && Index < List->Scroll + Height; Index++) {
    CONST CHAR8 *Label;
    CHAR8        Value[MODEL_LEN + 16];

    Value[0] = 0;
    Row (A, Index, &Label, Value, sizeof (Value));
    RowDraw (
      BODY_Y + (UINT32)(Index - List->Scroll), Index == List->Focus, Label,
      Value, G->Cols - MARGIN
    );
  }
  MoreMarks (
    BODY_Y, BODY_Y + (UINT32)Height, List->Scroll > 0,
    List->Scroll + Height < Count
  );
  FooterDraw (A, Verb, TRUE);
}

// "Firmware default", each model, "Back".
STATIC VOID
ModelRow (
  IN CONST APP     *A,
  IN UINTN          Index,
  OUT CONST CHAR8 **Label,
  OUT CHAR8        *Value,
  IN UINTN          Cap
)
{
  CONST CHAR8 *Current = A->Settings.Model;

  if (Index == 0) {
    *Label = gStrings->FirmwareDefault;
  } else if (Index <= A->ModelCount) {
    *Label = A->Models[Index - 1].Name;
  } else {
    *Label = gStrings->Back;
    return;
  }
  if (Index == 0 ? !Current[0] : AsciiStrEq (*Label, Current)) {
    AsciiStrCopy (Value, Cap, gStrings->Current);
  }
}

// Each entry, then "Back". Start tries the first.
STATIC VOID
EntryRow (
  IN CONST APP     *A,
  IN UINTN          Index,
  OUT CONST CHAR8 **Label,
  OUT CHAR8        *Value,
  IN UINTN          Cap
)
{
  if (Index >= A->EntryCount) {
    *Label = gStrings->Back;
    return;
  }
  *Label = A->Entries[Index].Title;
  if (A->Entries[Index].Bad) {
    AsciiStrCopy (Value, Cap, gStrings->NoTriesLeft);
  } else if (Index == 0) {
    AsciiStrCopy (Value, Cap, gStrings->Default);
  }
}

// A label and its value, or a bare line of the log.
STATIC VOID
TextRow (
  IN CONST APP     *A,
  IN UINTN          Index,
  OUT CONST CHAR8 **Label,
  OUT CHAR8        *Value,
  IN UINTN          Cap
)
{
  CONST TEXT_LINE *Line = &A->Lines[Index];

  if (!Line->Label[0]) {
    *Label = Line->Value;
    return;
  }
  *Label = Line->Label;
  AsciiStrCopy (Value, Cap, Line->Value);
}

// Screens without a list

// Where the launcher puts its Launching screen's text on its 53 x 20 grid,
// scaled to this one.
STATIC VOID
Block (
  OUT UINT32 *X,
  OUT UINT32 *Y,
  OUT UINT32 *Width
)
{
  GRID *G = &mView.Grid;

  *X     = G->Cols * 11 / GRID_MIN_COLS;
  *Y     = G->Rows * 6 / GRID_MIN_ROWS;
  *Width = G->Cols > *X + MARGIN ? G->Cols - *X - MARGIN : 1;
}

// Wraps Text from row *Y to at most the rows above Bottom, and moves *Y
// past it and Gap blank rows.
STATIC VOID
Paragraph (
  IN UINT32       X,
  IN OUT UINT32  *Y,
  IN UINT32       Width,
  IN UINT32       Bottom,
  IN CONST CHAR8 *Text,
  IN ATTR         Attr,
  IN UINT32       Gap
)
{
  if (!Text[0] || *Y >= Bottom) {
    return;
  }
  *Y += GridWrap (&mView.Grid, X, *Y, Width, Bottom - *Y, Text, Attr) + Gap;
}

// What is happening while the screen takes no input: Title, then Line and
// Detail, each after a blank row.
STATIC VOID
StatusDraw (
  IN CONST APP   *A,
  IN CONST CHAR8 *Title,
  IN CONST CHAR8 *Line,
  IN CONST CHAR8 *Detail OPTIONAL
)
{
  UINT32 X, Y, Width;
  UINT32 Bottom = mView.Grid.Rows;

  HeaderDraw (A, NULL);
  Block (&X, &Y, &Width);
  Paragraph (X, &Y, Width, Bottom, Title, ATTR_TEXT, 1);
  Paragraph (X, &Y, Width, Bottom, Line, ATTR_TEXT, 1);
  if (Detail) {
    Paragraph (X, &Y, Width, Bottom, Detail, ATTR_TEXT, 1);
  }
}

STATIC VOID
BootingDraw (IN CONST APP *A)
{
  CONST CHAR8 *Title = A->Booting >= 0 ? AppBootTitle (A) : NULL;

  if (!Title) {
    Title = TargetName (A, A->Starting);
  }
  StatusDraw (A, Title, gStrings->Starting, NULL);
}

// What went wrong, in bright, then why and what to do.
STATIC VOID
ErrorDraw (IN CONST APP *A)
{
  CONST CHAR8 *Title, *Message, *Hint;
  CONST CHAR8 *Verb = gStrings->VerbMenu;
  UINT32       X, Y, Width;
  UINT32       Bottom = mView.Grid.Rows - 2;

  switch (A->Error) {
  case BOOT_ERROR_NO_IMAGE:
    Title   = gStrings->ErrNoImageTitle;
    Message = gStrings->ErrNoImageMsg;
    Hint    = gStrings->ErrNoImageHint;
    break;
  case BOOT_ERROR_LOAD_FAILED:
    Title   = gStrings->ErrLoadTitle;
    Message = A->ErrorDetail;
    Hint    = gStrings->ErrLoadHint;
    break;
  case BOOT_ERROR_ANDROID:
    Title   = gStrings->ErrAndroidTitle;
    Message = gStrings->ErrAndroidMsg;
    Hint    = gStrings->ErrAndroidHint;
    break;
  case BOOT_ERROR_ANDROID_FAILED:
    Title   = gStrings->ErrAndroidFailedTitle;
    Message = gStrings->ErrAndroidFailedMsg;
    Hint    = gStrings->ErrAndroidFailedHint;
    break;
  case BOOT_ERROR_WIPE_BY_HAND:
    Title   = gStrings->ErrWipeTitle;
    Message = gStrings->WipeByHand;
    Hint    = "";
    break;
  case BOOT_ERROR_UNINSTALL:
    Title   = gStrings->UninstallFailed;
    Message = A->ErrorDetail;
    Hint    = "";
    break;
  default:
    Title   = gStrings->ErrModelTitle;
    Message = gStrings->ErrModelMsg;
    Hint    = "";
    Verb    = gStrings->VerbSelect;
    break;
  }
  HeaderDraw (A, NULL);
  Block (&X, &Y, &Width);
  Paragraph (X, &Y, Width, Bottom, Title, ATTR_BRIGHT, 1);
  Paragraph (X, &Y, Width, Bottom, Message, ATTR_TEXT, 1);
  Paragraph (X, &Y, Width, Bottom, Hint, ATTR_TEXT, 1);
  FooterDraw (A, Verb, FALSE);
}

// A partition's size in decimal units, as storage is sold: "2.1 GB" under
// 100 GB, "105 GB" from there, and MB under 1 GB.
STATIC VOID
SizeText (
  IN UINT64  Bytes,
  OUT CHAR8 *Out,
  IN UINTN   Cap
)
{
  UINT64 Tenths = (Bytes + 50000000) / 100000000;

  if (Tenths < 10) {
    SPrint (Out, Cap, "%llu MB", (Bytes + 500000) / 1000000);
  } else if (Tenths < 1000) {
    SPrint (Out, Cap, "%llu.%llu GB", Tenths / 10, Tenths % 10);
  } else {
    SPrint (Out, Cap, "%llu GB", (Tenths + 5) / 10);
  }
}

// The question, what it does, the partitions an uninstall erases, then
// Cancel and the action to pick from.
STATIC VOID
ConfirmDraw (IN CONST APP *A)
{
  GRID        *G         = &mView.Grid;
  BOOLEAN      Uninstall = A->Confirm == CONFIRM_UNINSTALL;
  UINTN        Parts     = Uninstall ? A->CfwPartCount : 0;
  CONST CHAR8 *Message   = Uninstall ? gStrings->ConfirmUninstallMsg
                                     : gStrings->ConfirmEdlMsg;
  UINT32       Bottom    = G->Rows - 2;
  // Under the message: the partitions after a blank row, then a blank row
  // and the two choices.
  UINT32       Below = (Parts ? (UINT32)Parts + 1 : 0) + 3;
  UINT32       Y     = BODY_Y + 2;

  HeaderDraw (A, Uninstall ? gStrings->UninstallPortareOs : gStrings->Edl);
  PutsCut (
    CARET_X, BODY_Y, G->Cols - CARET_X - MARGIN,
    Uninstall ? gStrings->ConfirmUninstallTitle : gStrings->ConfirmEdlTitle,
    ATTR_TEXT
  );
  // The message gives way when the rest wouldn't fit.
  Paragraph (
    TEXT_X, &Y, G->Cols - TEXT_X - MARGIN,
    Bottom > Y + Below ? Bottom - Below : Y + 1, Message, ATTR_TEXT, 0
  );
  if (Parts) {
    Y++;
    for (UINTN Index = 0; Index < Parts; Index++) {
      CHAR8 Size[24];

      SizeText (A->CfwParts[Index].Bytes, Size, sizeof (Size));
      RowDraw (Y++, FALSE, A->CfwParts[Index].Name, Size, G->Cols - 4);
    }
  }
  Y++;
  RowDraw (Y++, !A->ConfirmYes, gStrings->Cancel, NULL, G->Cols - MARGIN);
  RowDraw (
    Y, A->ConfirmYes,
    Uninstall ? gStrings->ConfirmUninstall : gStrings->ConfirmEdl, NULL,
    G->Cols - MARGIN
  );
  FooterDraw (A, gStrings->VerbSelect, TRUE);
}

// Frames

// Puts Area of the picture on the panel, rotated.
STATIC VOID
PresentArea (IN GRID_AREA Area)
{
  UINT32       *Out    = mView.Scratch;
  CONST UINT32 *Pixels = mView.Grid.Pixels;
  INT32         W      = mView.Grid.W;
  INT32         H      = mView.Grid.H;
  INT32         AreaW  = Area.X1 - Area.X0;
  INT32         AreaH  = Area.Y1 - Area.Y0;
  PRESENT_BLIT  Blit   = mView.Panel.Present;

  if (mView.Panel.Logical) {
    for (INT32 Y = Area.Y0; Y < Area.Y1; Y++) {
      CopyMem (
        &mView.Panel.Logical[(UINTN)Y * W + Area.X0],
        &Pixels[(UINTN)Y * W + Area.X0], (UINTN)AreaW * 4
      );
    }
  }
  switch (mView.Rotation) {
  case 0:
    Blit (
      Pixels + (UINTN)Area.Y0 * W + Area.X0, (UINT32)W, (UINT32)Area.X0,
      (UINT32)Area.Y0, (UINT32)AreaW, (UINT32)AreaH
    );
    return;
  case 90:
    // (X, Y) goes to (H - 1 - Y, X).
    for (INT32 Y = Area.Y0; Y < Area.Y1; Y++) {
      CONST UINT32 *Row    = Pixels + (UINTN)Y * W;
      UINT32       *Column = Out + (Area.Y1 - 1 - Y);
      for (INT32 X = Area.X0; X < Area.X1; X++) {
        Column[(UINTN)(X - Area.X0) * AreaH] = Row[X];
      }
    }
    Blit (
      Out, (UINT32)AreaH, (UINT32)(H - Area.Y1), (UINT32)Area.X0, (UINT32)AreaH,
      (UINT32)AreaW
    );
    return;
  case 180:
    for (INT32 Y = Area.Y0; Y < Area.Y1; Y++) {
      CONST UINT32 *Row  = Pixels + (UINTN)Y * W;
      UINT32       *Line = Out + (UINTN)(Area.Y1 - 1 - Y) * AreaW;
      for (INT32 X = Area.X0; X < Area.X1; X++) {
        Line[Area.X1 - 1 - X] = Row[X];
      }
    }
    Blit (
      Out, (UINT32)AreaW, (UINT32)(W - Area.X1), (UINT32)(H - Area.Y1),
      (UINT32)AreaW, (UINT32)AreaH
    );
    return;
  default:
    // 270: (X, Y) goes to (Y, W - 1 - X).
    for (INT32 Y = Area.Y0; Y < Area.Y1; Y++) {
      CONST UINT32 *Row    = Pixels + (UINTN)Y * W;
      UINT32       *Column = Out + (Y - Area.Y0);
      for (INT32 X = Area.X0; X < Area.X1; X++) {
        Column[(UINTN)(Area.X1 - 1 - X) * AreaH] = Row[X];
      }
    }
    Blit (
      Out, (UINT32)AreaH, (UINT32)Area.Y0, (UINT32)(W - Area.X1), (UINT32)AreaH,
      (UINT32)AreaW
    );
    return;
  }
}

// Draws the cells that changed, or with Everything the whole picture, and
// puts them on the panel.
STATIC VOID
Flush (IN BOOLEAN Everything)
{
  GRID_AREA Area;

  if (!mView.Ready || (!mView.Pending && !Everything)) {
    return;
  }
  mView.Pending = FALSE;
  if (GridFlush (&mView.Grid, Everything, &Area)) {
    PresentArea (Area);
  }
}

BOOLEAN
ViewInit (IN CONST PANEL *Panel)
{
  ZeroMem (&mView, sizeof (mView));
  mView.Panel   = *Panel;
  mView.Scratch = AllocatePool ((UINTN)Panel->W * Panel->H * 4);
  if (!mView.Scratch) {
    return FALSE;
  }
  mView.Ready = TRUE;
  ViewRotate (0);
  return mView.Ready;
}

VOID
ViewRotate (IN UINT16 Degrees)
{
  UINT32 W;
  UINT32 H;

  if (!mView.Scratch) {
    return;
  }
  mView.Rotation = Degrees % 360;
  LogicalSize (mView.Panel.W, mView.Panel.H, mView.Rotation, &W, &H);
  GridFree (&mView.Grid);
  // A new grid draws all of itself on the next flush.
  mView.Ready   = GridInit (&mView.Grid, (INT32)W, (INT32)H);
  mView.Pending = TRUE;
}

VOID
ViewLogicalSize (
  OUT UINT32 *W,
  OUT UINT32 *H
)
{
  LogicalSize (mView.Panel.W, mView.Panel.H, mView.Rotation, W, H);
}

VOID
ViewSync (IN OUT APP *A)
{
  if (!mView.Ready) {
    return;
  }
  GridClear (&mView.Grid);
  switch (A->Screen) {
  case SCREEN_MENU:
    MenuDraw (A);
    break;
  case SCREEN_CONFIRM:
    ConfirmDraw (A);
    break;
  case SCREEN_MODELS:
    ListDraw (
      A, gStrings->DeviceModel, A->ModelCount + 2, &A->ModelList, ModelRow,
      gStrings->VerbSelect
    );
    break;
  case SCREEN_ENTRIES:
    ListDraw (
      A, gStrings->BootEntries, A->EntryCount + 1, &A->EntryList, EntryRow,
      gStrings->VerbLaunch
    );
    break;
  case SCREEN_LINES:
    ListDraw (
      A, A->LinesTitle, A->LineCount, &A->InfoList, TextRow,
      gStrings->VerbMenu
    );
    break;
  case SCREEN_SCANNING:
    StatusDraw (A, gStrings->LookingForDts, gStrings->FewSeconds, NULL);
    break;
  case SCREEN_BOOTING:
    BootingDraw (A);
    break;
  case SCREEN_UNINSTALLING:
    StatusDraw (A, gStrings->Uninstalling, gStrings->DontTurnOff, NULL);
    break;
  case SCREEN_RESTARTING:
    StatusDraw (
      A, gStrings->RestartingRecovery,
      A->Wiped ? gStrings->CfwRemoved : gStrings->WipeInRecovery, NULL
    );
    break;
  case SCREEN_ERROR:
    ErrorDraw (A);
    break;
  }
  mView.Pending = TRUE;
  A->Dirty      = FALSE;
}

VOID
ViewRun (VOID)
{
  Flush (FALSE);
}

VOID
ViewSettle (VOID)
{
  Flush (FALSE);
}

VOID
ViewDone (VOID)
{
  GridFree (&mView.Grid);
  FreePool (mView.Scratch);
  ZeroMem (&mView, sizeof (mView));
}

VOID
ViewRedraw (IN BOOLEAN Everything)
{
  Flush (Everything);
}
