/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Frame - microui's command list drawn onto a canvas and put on the panel.
*/

#include "View/Frame.h"

#include "Base/Memory.h"
#include "Base/Text.h"
#include "Gfx/Gfx.h"
#include "View/Text.h"

// Cell side in pixels.
#define CELL 32
// Masks, draw commands and redrawn rectangles per frame.
#define MAX_MASKS 1024
#define MAX_DRAWS 4096
#define MAX_RECTS 128
// Ids at and above this are masks.
#define MASK_ID    0x1000
#define BACKGROUND 0x000000

// X0 <= x < X1, Y0 <= y < Y1. Empty when X0 >= X1 or Y0 >= Y1.
typedef struct _BOX {
  INT32 X0, Y0, X1, Y1;
} BOX;

STATIC struct {
  PANEL       Panel;
  mu_Context *Ctx;
  UINT32     *Pixels;
  UINT32     *Scratch;
  UINT16      Rotation;
  INT32       W, H;
  INT32       CellsW, CellsH;
  // This frame's cell hashes, and those of the picture on the panel.
  UINT32 *Cells;
  UINT32 *Shown;
  // What each draw command can touch, in command order.
  BOX  *Boxes;
  UINTN BoxCount;
  // Masks the commands draw, by id.
  CONST MASK *Masks[MAX_MASKS];
  UINTN       MaskCount;
  BOOLEAN     Built;   // the command list is for the current size
  BOOLEAN     Pending; // it hasn't been drawn
  BOOLEAN     Full;    // the panel needs all of it
} mFrame;

// microui measures text with these.
STATIC INT32
MuTextWidth (
  IN mu_Font      Font,
  IN CONST CHAR8 *String,
  IN INT32        Length
)
{
  return TextWidth (Font, String, Length < 0 ? MAX_UINTN : (UINTN)Length);
}

STATIC INT32
MuTextHeight (IN mu_Font Font)
{
  return ((CONST TEXT_FONT *)Font)->LineH;
}

BOOLEAN
FrameInit (IN CONST PANEL *Panel)
{
  UINTN Pixels = (UINTN)Panel->W * Panel->H;
  UINTN Cells =
    (UINTN)((Panel->W + CELL - 1) / CELL) * ((Panel->H + CELL - 1) / CELL);

  ZeroMem (&mFrame, sizeof (mFrame));
  mFrame.Panel   = *Panel;
  mFrame.Pixels  = AllocatePool (Pixels * 4);
  mFrame.Scratch = AllocatePool (Pixels * 4);
  mFrame.Cells   = AllocatePool (Cells * 4);
  mFrame.Shown   = AllocatePool (Cells * 4);
  mFrame.Boxes   = AllocatePool (MAX_DRAWS * sizeof (BOX));
  mFrame.Ctx     = AllocatePool (sizeof (mu_Context));
  if (!mFrame.Pixels || !mFrame.Scratch || !mFrame.Cells || !mFrame.Shown ||
      !mFrame.Boxes || !mFrame.Ctx) {
    return FALSE;
  }
  mu_init (mFrame.Ctx);
  mFrame.Ctx->text_width            = MuTextWidth;
  mFrame.Ctx->text_height           = MuTextHeight;
  mFrame.Ctx->style->padding        = 0;
  mFrame.Ctx->style->spacing        = 0;
  mFrame.Ctx->style->scrollbar_size = 0;
  FrameRotate (0);
  return TRUE;
}

VOID
FrameDone (VOID)
{
  FreePool (mFrame.Pixels);
  FreePool (mFrame.Scratch);
  FreePool (mFrame.Cells);
  FreePool (mFrame.Shown);
  FreePool (mFrame.Boxes);
  FreePool (mFrame.Ctx);
  ZeroMem (&mFrame, sizeof (mFrame));
}

VOID
FrameRotate (IN UINT16 Degrees)
{
  UINT32 W;
  UINT32 H;

  mFrame.Rotation = Degrees % 360;
  LogicalSize (mFrame.Panel.W, mFrame.Panel.H, mFrame.Rotation, &W, &H);
  mFrame.W       = (INT32)W;
  mFrame.H       = (INT32)H;
  mFrame.CellsW  = (mFrame.W + CELL - 1) / CELL;
  mFrame.CellsH  = (mFrame.H + CELL - 1) / CELL;
  mFrame.Built   = FALSE;
  mFrame.Pending = TRUE;
  mFrame.Full    = TRUE;
}

VOID
FrameSize (
  OUT INT32 *W,
  OUT INT32 *H
)
{
  *W = mFrame.W;
  *H = mFrame.H;
}

VOID
FrameInvalidate (VOID)
{
  mFrame.Full = TRUE;
}

mu_Context *
FrameBegin (VOID)
{
  mFrame.MaskCount = 0;
  mu_begin (mFrame.Ctx);
  return mFrame.Ctx;
}

VOID
FrameEnd (VOID)
{
  mu_end (mFrame.Ctx);
  mFrame.Built   = TRUE;
  mFrame.Pending = TRUE;
}

INT32
FrameMaskId (IN CONST MASK *Mask)
{
  if (mFrame.MaskCount == MAX_MASKS) {
    return -1;
  }
  mFrame.Masks[mFrame.MaskCount] = Mask;
  return MASK_ID + (INT32)mFrame.MaskCount++;
}

// Hashing

STATIC UINT32
Hash (
  IN UINT32      Hash,
  IN CONST VOID *Data,
  IN UINTN       Size
)
{
  CONST UINT8 *Bytes = Data;

  for (UINTN Index = 0; Index < Size; Index++) {
    Hash = (Hash ^ Bytes[Index]) * 16777619u;
  }
  return Hash;
}

// Folds Value into a cell's hash. The order matters.
STATIC UINT32
Mix (
  IN UINT32 Cell,
  IN UINT32 Value
)
{
  Cell = (Cell ^ Value) * 0x9e3779b1u;
  return Cell ^ (Cell >> 15);
}

STATIC BOX
Intersect (
  IN BOX A,
  IN BOX B
)
{
  BOX Out = {A.X0 > B.X0 ? A.X0 : B.X0, A.Y0 > B.Y0 ? A.Y0 : B.Y0,
             A.X1 < B.X1 ? A.X1 : B.X1, A.Y1 < B.Y1 ? A.Y1 : B.Y1};
  return Out;
}

STATIC BOOLEAN
IsEmpty (IN BOX B)
{
  return B.X0 >= B.X1 || B.Y0 >= B.Y1;
}

STATIC BOX
BoxOf (IN mu_Rect R)
{
  return (BOX){R.x, R.y, R.x + R.w, R.y + R.h};
}

STATIC UINT32
ColorOf (IN mu_Color Color)
{
  return (UINT32)Color.r << 16 | (UINT32)Color.g << 8 | Color.b;
}

STATIC CONST MASK *
MaskOf (IN INT32 Id)
{
  UINTN Index = (UINTN)(Id - MASK_ID);

  return Id >= MASK_ID && Index < mFrame.MaskCount ? mFrame.Masks[Index] : NULL;
}

// Works out what each draw command can touch and its hash, and folds the
// hash into those cells. A fill changes only the pixels it covers, so each
// cell gets the hash of its own piece: a bar that shrinks changes only the
// cell where it ends.
STATIC VOID
HashCells (VOID)
{
  UINTN       Count   = (UINTN)mFrame.CellsW * mFrame.CellsH;
  BOX         Whole   = {0, 0, mFrame.W, mFrame.H};
  BOX         Clip    = Whole;
  mu_Command *Command = NULL;

  for (UINTN Index = 0; Index < Count; Index++) {
    mFrame.Cells[Index] = 2166136261u;
  }
  mFrame.BoxCount = 0;
  while (mFrame.Built && mu_next_command (mFrame.Ctx, &Command)) {
    BOX     Box   = {0};
    UINT32  Code  = 2166136261u;
    BOOLEAN Split = FALSE;

    switch (Command->type) {
    case MU_COMMAND_CLIP:
      Clip = Intersect (BoxOf (Command->clip.rect), Whole);
      continue;
    case MU_COMMAND_RECT:
      Box   = BoxOf (Command->rect.rect);
      Code  = Hash (Code, &Command->rect.color, sizeof (mu_Color));
      Split = TRUE;
      break;
    case MU_COMMAND_TEXT: {
      CONST TEXT_FONT *Font = Command->text.font;
      CONST CHAR8     *Text = Command->text.str;
      if (!TextBounds (
            Font, Command->text.pos.x, Command->text.pos.y, Text, MAX_UINTN,
            &Box.X0, &Box.Y0, &Box.X1, &Box.Y1
          )) {
        Box = (BOX){0};
      }
      Code = Hash (Code, &Font->Px, sizeof (Font->Px));
      Code = Hash (Code, &Font->Weight, sizeof (Font->Weight));
      Code = Hash (Code, &Command->text.pos, sizeof (mu_Vec2));
      Code = Hash (Code, &Command->text.color, sizeof (mu_Color));
      Code = Hash (Code, Text, AsciiStrLen (Text));
      break;
    }
    case MU_COMMAND_ICON: {
      CONST MASK *Mask = MaskOf (Command->icon.id);
      Box              = BoxOf (Command->icon.rect);
      Code             = Hash (Code, &Mask, sizeof (Mask));
      Code             = Hash (Code, &Command->icon.rect, sizeof (mu_Rect));
      Code             = Hash (Code, &Command->icon.color, sizeof (mu_Color));
      break;
    }
    default:
      continue;
    }
    Box = Intersect (Box, Clip);
    if (!Split) {
      Code = Hash (Code, &Clip, sizeof (Clip));
    }
    if (mFrame.BoxCount < MAX_DRAWS) {
      mFrame.Boxes[mFrame.BoxCount] = Box;
    }
    mFrame.BoxCount++;
    if (IsEmpty (Box)) {
      continue;
    }
    for (INT32 Cy = Box.Y0 / CELL; Cy <= (Box.Y1 - 1) / CELL; Cy++) {
      UINT32 *Row = mFrame.Cells + (UINTN)Cy * mFrame.CellsW;
      for (INT32 Cx = Box.X0 / CELL; Cx <= (Box.X1 - 1) / CELL; Cx++) {
        UINT32 Piece = Code;
        if (Split) {
          BOX Cell = {Cx * CELL, Cy * CELL, (Cx + 1) * CELL, (Cy + 1) * CELL};
          Cell     = Intersect (Box, Cell);
          Piece    = Hash (Code, &Cell, sizeof (Cell));
        }
        Row[Cx] = Mix (Row[Cx], Piece);
      }
    }
  }
}

// Drawing.

// Opacities at or above this draw solid.
#define OPAQUE 253

// Blends Rgb over Dest by Mix out of 255.
STATIC UINT32
MixPixel (
  IN UINT32 Dest,
  IN UINT32 Rgb,
  IN UINT32 Mix
)
{
  UINT32 Inverse = 255 - Mix;
  UINT32 RedBlue = (Rgb & 0xff00ff) * Mix + (Dest & 0xff00ff) * Inverse;
  UINT32 Green   = (Rgb & 0x00ff00) * Mix + (Dest & 0x00ff00) * Inverse;

  return (RedBlue >> 8 & 0xff00ff) | (Green >> 8 & 0x00ff00);
}

// Fills the part of R inside Clip.
STATIC VOID
FillArea (
  IN BOX     Clip,
  IN mu_Rect R,
  IN UINT32  Rgb,
  IN UINT8   Opacity
)
{
  BOX Area = Intersect (BoxOf (R), Clip);

  for (INT32 Y = Area.Y0; Y < Area.Y1; Y++) {
    UINT32 *Row = mFrame.Pixels + (UINTN)Y * mFrame.W;
    if (Opacity >= OPAQUE) {
      for (INT32 X = Area.X0; X < Area.X1; X++) {
        Row[X] = Rgb;
      }
    } else {
      for (INT32 X = Area.X0; X < Area.X1; X++) {
        Row[X] = MixPixel (Row[X], Rgb, Opacity);
      }
    }
  }
}

// Blends a W x H coverage mask in Rgb with its top left at (X, Y), inside
// Clip.
STATIC VOID
BlendMask (
  IN BOX          Clip,
  IN INT32        X,
  IN INT32        Y,
  IN INT32        W,
  IN INT32        H,
  IN CONST UINT8 *Data,
  IN UINT32       Rgb,
  IN UINT8        Opacity
)
{
  BOX Area = Intersect ((BOX){X, Y, X + W, Y + H}, Clip);

  for (INT32 Line = Area.Y0; Line < Area.Y1; Line++) {
    UINT32      *Row   = mFrame.Pixels + (UINTN)Line * mFrame.W;
    CONST UINT8 *Cover = Data + (UINTN)(Line - Y) * W - X;
    for (INT32 Column = Area.X0; Column < Area.X1; Column++) {
      UINT32 Mix = Cover[Column];
      if (Opacity < OPAQUE) {
        Mix = Mix * Opacity >> 8;
      }
      if (Mix >= OPAQUE) {
        Row[Column] = Rgb;
      } else if (Mix) {
        Row[Column] = MixPixel (Row[Column], Rgb, Mix);
      }
    }
  }
}

// Draws a text command's glyphs inside Clip.
STATIC VOID
DrawGlyphs (
  IN BOX                   Clip,
  IN CONST mu_TextCommand *Text
)
{
  CONST TEXT_FONT *Font     = Text->font;
  CONST CHAR8     *String   = Text->str;
  INT32            X        = Text->pos.x;
  INT32            Baseline = Text->pos.y + Font->LineH - Font->Base;

  while (*String) {
    UINTN             Length;
    CONST FONT_GLYPH *Glyph =
      FontGlyph (Font->Px, Font->Weight, Utf8Next (String, &Length), TRUE);

    if (Glyph && Glyph->Mask) {
      BlendMask (
        Clip, X + Glyph->Left, Baseline + Glyph->Top, Glyph->W, Glyph->H,
        Glyph->Mask, ColorOf (Text->color), Text->color.a
      );
    }
    X += Glyph ? (INT32)RoundF (Glyph->Advance) : 0;
    String += Length;
  }
}

// Draws Command inside Clip.
STATIC VOID
Replay (
  IN BOX               Clip,
  IN CONST mu_Command *Command
)
{
  CONST MASK *Mask;

  switch (Command->type) {
  case MU_COMMAND_RECT:
    if (Command->rect.color.a) {
      FillArea (
        Clip, Command->rect.rect, ColorOf (Command->rect.color),
        Command->rect.color.a
      );
    }
    break;
  case MU_COMMAND_TEXT:
    if (Command->text.color.a) {
      DrawGlyphs (Clip, &Command->text);
    }
    break;
  case MU_COMMAND_ICON:
    Mask = MaskOf (Command->icon.id);
    if (Mask && Command->icon.color.a) {
      BlendMask (
        Clip, Command->icon.rect.x, Command->icon.rect.y, Mask->W, Mask->H,
        Mask->Data, ColorOf (Command->icon.color), Command->icon.color.a
      );
    }
    break;
  default:
    break;
  }
}

// Paints Area with the background and every command that reaches it.
STATIC VOID
Repaint (IN BOX Area)
{
  mu_Command *Command = NULL;
  UINTN       Index   = 0;

  FillArea (
    Area, mu_rect (Area.X0, Area.Y0, Area.X1 - Area.X0, Area.Y1 - Area.Y0),
    BACKGROUND, 255
  );
  while (mFrame.Built && mu_next_command (mFrame.Ctx, &Command)) {
    if (Command->type == MU_COMMAND_CLIP) {
      continue;
    }
    // Past MAX_DRAWS the boxes aren't known, so clip to the area alone.
    BOX Box = Index < MAX_DRAWS ? Intersect (mFrame.Boxes[Index], Area) : Area;
    Index++;
    if (!IsEmpty (Box)) {
      Replay (Box, Command);
    }
  }
}

// Puts Area of the picture on the panel, rotated.
STATIC VOID
PresentArea (IN BOX Area)
{
  UINT32       *Out    = mFrame.Scratch;
  CONST UINT32 *Pixels = mFrame.Pixels;
  INT32         W      = mFrame.W;
  INT32         H      = mFrame.H;
  INT32         AreaW  = Area.X1 - Area.X0;
  INT32         AreaH  = Area.Y1 - Area.Y0;
  PRESENT_BLIT  Blit   = mFrame.Panel.Present;

  if (mFrame.Panel.Logical) {
    for (INT32 Y = Area.Y0; Y < Area.Y1; Y++) {
      CopyMem (
        &mFrame.Panel.Logical[(UINTN)Y * W + Area.X0],
        &Pixels[(UINTN)Y * W + Area.X0], (UINTN)AreaW * 4
      );
    }
  }
  switch (mFrame.Rotation) {
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

VOID
FrameDraw (IN BOOLEAN Everything)
{
  STATIC BOX Rects[MAX_RECTS];
  UINTN      RectCount = 0;
  BOX        Whole     = {0, 0, mFrame.W, mFrame.H};
  BOX        Union     = {0};
  BOOLEAN    All;
  UINT32    *Swap;

  if (!mFrame.Ctx || (!mFrame.Pending && !Everything)) {
    return;
  }
  if (mFrame.Pending) {
    HashCells ();
    Swap         = mFrame.Shown;
    mFrame.Shown = mFrame.Cells;
    mFrame.Cells = Swap;
  }
  All            = Everything || mFrame.Full;
  mFrame.Pending = FALSE;
  mFrame.Full    = FALSE;

  // Runs of changed cells along each row, joined with the rectangle above
  // when they span the same columns.
  for (INT32 Cy = 0; Cy < mFrame.CellsH && !All; Cy++) {
    CONST UINT32 *Now    = mFrame.Shown + (UINTN)Cy * mFrame.CellsW;
    CONST UINT32 *Before = mFrame.Cells + (UINTN)Cy * mFrame.CellsW;
    for (INT32 Cx = 0; Cx < mFrame.CellsW; Cx++) {
      if (Now[Cx] == Before[Cx]) {
        continue;
      }
      INT32 End = Cx + 1;
      while (End < mFrame.CellsW && Now[End] != Before[End]) {
        End++;
      }
      BOX     Run    = {Cx * CELL, Cy * CELL, End * CELL, (Cy + 1) * CELL};
      BOOLEAN Joined = FALSE;
      for (UINTN Index = RectCount; Index-- > 0;) {
        if (Rects[Index].Y1 == Run.Y0 && Rects[Index].X0 == Run.X0 &&
            Rects[Index].X1 == Run.X1) {
          Rects[Index].Y1 = Run.Y1;
          Joined          = TRUE;
          break;
        }
      }
      if (!Joined) {
        if (RectCount == MAX_RECTS) {
          All = TRUE;
          break;
        }
        Rects[RectCount++] = Run;
      }
      Cx = End;
    }
  }
  if (All) {
    Rects[0]  = Whole;
    RectCount = 1;
  }
  for (UINTN Index = 0; Index < RectCount; Index++) {
    BOX Area = Intersect (Rects[Index], Whole);
    if (IsEmpty (Area)) {
      continue;
    }
    Repaint (Area);
    Union = IsEmpty (Union) ? Area
                            : (BOX){Area.X0 < Union.X0 ? Area.X0 : Union.X0,
                                    Area.Y0 < Union.Y0 ? Area.Y0 : Union.Y0,
                                    Area.X1 > Union.X1 ? Area.X1 : Union.X1,
                                    Area.Y1 > Union.Y1 ? Area.Y1 : Union.Y1};
  }
  if (!IsEmpty (Union)) {
    PresentArea (Union);
  }
}
