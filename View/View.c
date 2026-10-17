/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  View - the menu's screens, drawn with microui.

  Each frame describes the whole screen as microui commands, and Frame.c
  draws only what changed. A frame is made when the app changes and while
  something moves: the countdown bar, the boot screen's sweep, and the
  dialog and toast fading in. Lists jump to their new place instead of
  scrolling.
*/

#include "View/View.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Gfx/Gfx.h"
#include "Ui/Lang.h"
#include "View/Frame.h"
#include "View/Text.h"
#include "microui.h"

extern CONST UINT8 gOablAssetFontCjk[];
extern CONST UINTN gOablAssetFontCjkSize;

// Theme. The menu is dark because most panels are OLED and it is used in
// dim rooms; pure black keeps OLED pixels off.

#define THEME_CARD        0x171716
#define THEME_RAISED      0x1C1C1B
#define THEME_LINE        0x2A2A28
#define THEME_TEXT        0xF1EFE8
#define THEME_TEXT2       0xB4B2A9
#define THEME_TEXT3       0x888780
#define THEME_ACCENT      0x5DCAA5
#define THEME_ACCENT_BG   0x085041
#define THEME_ACCENT_TEXT 0xE1F5EE
#define THEME_DANGER      0xF09595
#define THEME_DANGER_BG   0x501313
#define THEME_DANGER_FILL 0xE24B4A
#define THEME_DANGER_TEXT 0xFCEBEB
#define THEME_BUTTON      0x2A2A28
#define THEME_TOAST       0x2C2C2A
// The red of "ABL" in the wordmark.
#define THEME_BRAND 0xFF5555

// An animation makes a frame at most this often.
#define FRAME_MS        16
#define OVERLAY_FADE_MS 120
#define TOAST_FADE_MS   150
#define SWEEP_MS        1100

// The windows: the screen, and the dialog and the toast over it.
#define WINDOW_OPTIONS                                                         \
  (MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOSCROLL)

// Lists longer than this show their first rows only.
#define MAX_LIST 512

// Geometry

typedef struct _RECT {
  INT32 X, Y, W, H;
} RECT;

STATIC INT32
Imax (
  IN INT32 A,
  IN INT32 B
)
{
  return A > B ? A : B;
}

STATIC INT32
Imin (
  IN INT32 A,
  IN INT32 B
)
{
  return A < B ? A : B;
}

STATIC RECT
Rect (
  IN INT32 X,
  IN INT32 Y,
  IN INT32 W,
  IN INT32 H
)
{
  return (RECT){X, Y, W, H};
}

// Rounds a rectangle in fractional pixels to the whole pixels it covers.
STATIC RECT
Rectf (
  IN float X,
  IN float Y,
  IN float W,
  IN float H
)
{
  INT32 X0 = (INT32)RoundF (X);
  INT32 Y0 = (INT32)RoundF (Y);
  INT32 X1 = (INT32)RoundF (X + W);
  INT32 Y1 = (INT32)RoundF (Y + H);
  return Rect (X0, Y0, Imax (X1 - X0, 0), Imax (Y1 - Y0, 0));
}

STATIC RECT
Inset (
  IN RECT  R,
  IN INT32 By
)
{
  return Rect (
    R.X + By, R.Y + By, Imax (R.W - 2 * By, 0), Imax (R.H - 2 * By, 0)
  );
}

// TakeTop, TakeBottom and TakeLeft split R, clamped to its size.
STATIC VOID
TakeTop (
  IN RECT   R,
  IN INT32  H,
  OUT RECT *Top,
  OUT RECT *Rest
)
{
  H     = Imin (Imax (H, 0), R.H);
  *Top  = Rect (R.X, R.Y, R.W, H);
  *Rest = Rect (R.X, R.Y + H, R.W, R.H - H);
}

STATIC VOID
TakeBottom (
  IN RECT   R,
  IN INT32  H,
  OUT RECT *Rest,
  OUT RECT *Bottom
)
{
  H       = Imin (Imax (H, 0), R.H);
  *Rest   = Rect (R.X, R.Y, R.W, R.H - H);
  *Bottom = Rect (R.X, R.Y + R.H - H, R.W, H);
}

STATIC VOID
TakeLeft (
  IN RECT   R,
  IN INT32  W,
  OUT RECT *Left,
  OUT RECT *Rest
)
{
  W     = Imin (Imax (W, 0), R.W);
  *Left = Rect (R.X, R.Y, W, R.H);
  *Rest = Rect (R.X + W, R.Y, R.W - W, R.H);
}

STATIC float
CenterX (IN RECT R)
{
  return (float)R.X + (float)R.W / 2;
}

STATIC float
CenterY (IN RECT R)
{
  return (float)R.Y + (float)R.H / 2;
}

STATIC INT32
Right (IN RECT R)
{
  return R.X + R.W;
}

STATIC INT32
Bottom (IN RECT R)
{
  return R.Y + R.H;
}

// State

STATIC struct {
  BOOLEAN Ready;
  UINT32 (*Clock) (VOID);
  // The app the last frame showed. Animation frames are made from it.
  APP        *App;
  mu_Context *Ctx;
  // Pixels per layout unit, and the picture's size.
  float   Unit;
  INT32   W, H;
  SCHEME  Scheme;
  BOOLEAN Laid;
  RECT    Header, Body, Footer;
  // The body as laid out, above the footer. A screen without a footer
  // extends it to the footer's bottom.
  RECT Content;
  // Opacity of what is drawn now, for the fades.
  UINT8 Alpha;
  // The clock when the frame was made, and whether something moves.
  UINT32  Now;
  UINT32  LastFrame;
  BOOLEAN Animating;
  // The countdown bar shrinks from BarLeft of BarTotal ms since BarTick.
  BOOLEAN BarShown;
  UINT64  BarUntil, BarLeft, BarTotal;
  UINT32  BarTick;
  BOOLEAN Sweeping;
  UINT32  SweepTick;
  BOOLEAN OverlayShown, OverlayFaded;
  CONFIRM OverlayKind;
  UINT32  OverlayTick;
  UINT64  ToastUntil; // the toast shown
  BOOLEAN ToastFaded;
  UINT32  ToastTick;
} mView;

STATIC float
ToPx (IN float Units)
{
  return Units * mView.Unit;
}

STATIC INT32
ToPxInt (IN float Units)
{
  return (INT32)RoundF (Units * mView.Unit);
}

// Adds the Size bytes at Data to Hash (FNV-1a).
STATIC UINT32
Digest (
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

// Adds String to Hash, including its NUL.
STATIC UINT32
DigestStr (
  IN UINT32       Hash,
  IN CONST CHAR8 *String
)
{
  return Digest (Hash, String, AsciiStrLen (String) + 1);
}

// Returns how far an animation of Duration ms started at Tick has got,
// from 0 to 1024. A running one asks for another frame.
STATIC INT32
Progress (
  IN UINT32 Tick,
  IN UINT32 Duration
)
{
  UINT32 Elapsed = mView.Now - Tick;

  if (Elapsed >= Duration) {
    return 1024;
  }
  mView.Animating = TRUE;
  return (INT32)((UINT64)Elapsed * 1024 / Duration);
}

// Returns how far a fade-in is, as an opacity. *Done skips it.
STATIC UINT8
Fade (
  IN UINT32       Tick,
  IN UINT32       Duration,
  IN OUT BOOLEAN *Done
)
{
  INT32 Step;

  if (*Done) {
    return 255;
  }
  Step = Progress (Tick, Duration);
  if (Step == 1024) {
    *Done = TRUE;
  }
  return (UINT8)(Step * 255 >> 10);
}

// The ease-in-out curve, cubic-bezier(0.42, 0, 0.58, 1), at T in 0..1.
STATIC float
EaseInOut (IN float T)
{
  float Low  = 0;
  float High = 1;
  float S    = T;

  // Find where the curve's x is T, then return its y there.
  for (INT32 Step = 0; Step < 24; Step++) {
    float U = 1 - S;
    float X = 3 * U * U * S * 0.42f + 3 * U * S * S * 0.58f + S * S * S;
    if (X < T) {
      Low = S;
    } else {
      High = S;
    }
    S = (Low + High) / 2;
  }
  return 3 * (1 - S) * S * S + S * S * S;
}

// Drawing, as microui commands

typedef enum { ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTER } ALIGN;

STATIC mu_Color
Color (
  IN UINT32 Rgb,
  IN UINT32 Opacity
)
{
  return mu_color (
    (INT32)(Rgb >> 16 & 0xff), (INT32)(Rgb >> 8 & 0xff), (INT32)(Rgb & 0xff),
    (INT32)(Opacity * mView.Alpha / 255)
  );
}

STATIC VOID
Fill (
  IN RECT   R,
  IN UINT32 Rgb,
  IN UINT32 Opacity
)
{
  mu_draw_rect (mView.Ctx, mu_rect (R.X, R.Y, R.W, R.H), Color (Rgb, Opacity));
}

// Draws Mask in Rgb with its top left at (X, Y).
STATIC VOID
DrawMask (
  IN INT32       X,
  IN INT32       Y,
  IN CONST MASK *Mask OPTIONAL,
  IN UINT32      Rgb
)
{
  INT32 Id = Mask ? FrameMaskId (Mask) : -1;

  if (Id >= 0) {
    mu_draw_icon (
      mView.Ctx, Id, mu_rect (X, Y, Mask->W, Mask->H), Color (Rgb, 255)
    );
  }
}

// A rounded rectangle. Radius is in layout units.
STATIC VOID
RoundRect (
  IN RECT   R,
  IN float  Radius,
  IN UINT32 Rgb
)
{
  DrawMask (R.X, R.Y, RoundRectMask (R.W, R.H, ToPxInt (Radius), 0), Rgb);
}

// Its outline, Width layout units wide (at least a pixel).
STATIC VOID
Ring (
  IN RECT   R,
  IN float  Radius,
  IN float  Width,
  IN UINT32 Rgb
)
{
  DrawMask (
    R.X, R.Y,
    RoundRectMask (
      R.W, R.H, ToPxInt (Radius), Imax ((INT32)RoundF (ToPx (Width)), 1)
    ),
    Rgb
  );
}

// A disc of Radius pixels centred on (Cx, Cy).
STATIC VOID
Disc (
  IN float  Cx,
  IN float  Cy,
  IN float  Radius,
  IN UINT32 Rgb
)
{
  RECT Bounds = Rectf (Cx - Radius, Cy - Radius, 2 * Radius, 2 * Radius);
  DrawMask (
    Bounds.X, Bounds.Y, RoundRectMask (Bounds.W, Bounds.H, Bounds.W / 2, 0), Rgb
  );
}

// Icon, Size pixels square and centred on (Cx, Cy).
STATIC VOID
Icon (
  IN ICON   Icon,
  IN float  Cx,
  IN float  Cy,
  IN float  Size,
  IN UINT32 Rgb
)
{
  float Origin = Size / 2 + IconPad (Size);

  DrawMask (
    (INT32)RoundF (Cx - Origin), (INT32)RoundF (Cy - Origin),
    IconMask (Icon, Size), Rgb
  );
}

// Draws Length bytes of String as a line of a label from X, W wide, with
// its baseline at Baseline, clipped to Clip. microui clips only text whose
// advance crosses the clip, so a glyph that reaches past it gets a clip
// command here.
STATIC VOID
Line (
  IN mu_Rect          Clip,
  IN INT32            X,
  IN INT32            W,
  IN float            Baseline,
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN UINTN            Length,
  IN UINT32           Rgb,
  IN ALIGN            Align
)
{
  INT32   Top   = (INT32)RoundF (Baseline) - (Font->LineH - Font->Base);
  INT32   LineW = TextWidth (Font, String, Length);
  INT32   TextX = Align == ALIGN_RIGHT    ? X + W - LineW
                  : Align == ALIGN_CENTER ? X + (W - LineW) / 2
                                          : X;
  INT32   X0, Y0, X1, Y1;
  BOOLEAN Over;

  mu_push_clip_rect (mView.Ctx, Clip);
  Clip = mu_get_clip_rect (mView.Ctx);
  Over = TextBounds (Font, TextX, Top, String, Length, &X0, &Y0, &X1, &Y1) &&
         (X0 < Clip.x || Y0 < Clip.y || X1 > Clip.x + Clip.w ||
          Y1 > Clip.y + Clip.h);
  if (Over) {
    mu_set_clip (mView.Ctx, Clip);
  }
  mu_draw_text (
    mView.Ctx, (mu_Font)Font, String, (INT32)Length, mu_vec2 (TextX, Top),
    Color (Rgb, 255)
  );
  if (Over) {
    mu_set_clip (mView.Ctx, mu_rect (0, 0, 0x1000000, 0x1000000));
  }
  mu_pop_clip_rect (mView.Ctx);
}

// Draws String in R at Size layout units, vertically centred on its cap
// height and cut short with an ellipsis to R's width, clipped to its line.
STATIC VOID
TextIn (
  IN RECT         R,
  IN CONST CHAR8 *String,
  IN float        Size,
  IN FONT_WEIGHT  Weight,
  IN UINT32       Rgb,
  IN ALIGN        Align
)
{
  CONST TEXT_FONT *Font     = TextFont (ToPx (Size), Weight);
  float            Baseline = CenterY (R) + FontCapHeight (ToPx (Size)) / 2;
  INT32            W        = Imax (R.W, 1);
  CHAR8            Fitted[320];

  TextFit (Font, String, R.W, Fitted, sizeof (Fitted));
  Line (
    mu_rect (
      R.X, (INT32)RoundF (Baseline) - (Font->LineH - Font->Base), W, Font->LineH
    ),
    R.X, W, Baseline, Font, Fitted, AsciiStrLen (Fitted), Rgb, Align
  );
}

STATIC float
Measure (
  IN CONST CHAR8 *String,
  IN float        Size,
  IN FONT_WEIGHT  Weight
)
{
  return (float)TextWidth (TextFont (ToPx (Size), Weight), String, MAX_UINTN);
}

STATIC INT32
WrapCount (
  IN CONST CHAR8 *String,
  IN float        Size,
  IN FONT_WEIGHT  Weight,
  IN float        Width
)
{
  return (INT32)TextWrap (
    TextFont (ToPx (Size), Weight), String, (INT32)Width, NULL, 0
  );
}

// Draws String wrapped to Width, lines LineH apart and centred on their cap
// height. At most MaxLines show, the last ending in dots if more follow.
STATIC VOID
Wrapped (
  IN float        X,
  IN float        Y,
  IN float        Width,
  IN CONST CHAR8 *String,
  IN float        Size,
  IN FONT_WEIGHT  Weight,
  IN float        LineH,
  IN INT32        MaxLines,
  IN UINT32       Rgb,
  IN ALIGN        Align
)
{
  CONST TEXT_FONT *Font    = TextFont (ToPx (Size), Weight);
  INT32            Pitch   = (INT32)RoundF (LineH);
  INT32            W       = (INT32)Width;
  INT32            Left    = (INT32)RoundF (X);
  INT32            Extra   = Font->LineH / 4;
  UINTN            Count   = TextWrap (Font, String, W, NULL, 0);
  UINTN            Letters = 0;
  INT32            First;
  INT32            Top;
  INT32            Height;
  CHAR8            Dotted[512];
  TEXT_SPAN        Lines[16];

  First = (INT32)RoundF (Y + LineH / 2 + FontCapHeight (ToPx (Size)) / 2);
  Top   = First - (Font->LineH - Font->Base);
  for (CONST CHAR8 *Char = String; *Char; Char++) {
    Letters += ((UINT8)*Char & 0xc0) != 0x80;
  }
  if (Count > (UINTN)MaxLines && Count > 1 && Letters > 3) {
    UINTN At = TextDotsAt (Font, String, W, (UINTN)MaxLines);
    if (At + 4 > sizeof (Dotted)) {
      At = sizeof (Dotted) - 4;
    }
    CopyMem (Dotted, String, At);
    CopyMem (Dotted + At, "...", 4);
    String = Dotted;
  }
  Count = TextWrap (Font, String, W, Lines, 16);
  Count = Count < (UINTN)MaxLines ? Count : (UINTN)MaxLines;
  Count = Count < 16 ? Count : 16;
  // Clipped only at the bottom. Elsewhere the text may draw a quarter line
  // past its box.
  Height = (Imax ((INT32)Count, 1) - 1) * Pitch + Font->LineH;
  for (UINTN Index = 0; Index < Count; Index++) {
    Line (
      mu_rect (Left - Extra, Top - Extra, W + 2 * Extra, Height + Extra), Left,
      W, (float)(First + (INT32)Index * Pitch), Font,
      String + Lines[Index].Start, Lines[Index].Length, Rgb, Align
    );
  }
}

// Lists

// A list in Area, Margin pixels kept above and below for the chevrons.
// First and End are the rows that show.
typedef struct _LIST {
  RECT           Area;
  INT32          Margin;
  UINTN          Count;
  CONST INT32   *Heights;
  CONST BOOLEAN *Headers; // NULL when there are none
  UINTN          First, End;
} LIST;

STATIC VOID
ListInit (
  OUT LIST         *List,
  IN RECT           Area,
  IN INT32          Margin,
  IN UINTN          Count,
  IN CONST INT32   *Heights,
  IN CONST BOOLEAN *Headers OPTIONAL
)
{
  *List = (LIST){.Area    = Area,
                 .Margin  = Margin,
                 .Count   = Count > MAX_LIST ? MAX_LIST : Count,
                 .Heights = Heights,
                 .Headers = Headers};
}

// Picks the rows to show so row Focus is in view. The list starts on a
// whole row (*Scroll, kept in the app) and moves only as far as needed to
// keep Focus on screen. A section header stays with the item below it,
// spare space at the end pulls rows back in, and only whole rows show.
STATIC VOID
ListPlace (
  IN OUT LIST  *List,
  IN UINTN      Focus,
  IN OUT UINTN *Scroll
)
{
  CONST INT32 *H     = List->Heights;
  UINTN        Count = List->Count;
  INT32        Avail = Imax (List->Area.H - 2 * List->Margin, 0);
  INT32        Total = 0;
  UINTN        First;
  UINTN        End;

  if (!Count) {
    return;
  }
  if (Focus >= Count) {
    Focus = Count - 1;
  }
  First = *Scroll < Focus ? *Scroll : Focus;
  if (First == Focus && Focus > 0 && List->Headers &&
      List->Headers[Focus - 1]) {
    First = Focus - 1;
  }
  while (First < Focus) {
    INT32 Sum = 0;
    for (UINTN Index = First; Index <= Focus; Index++) {
      Sum += H[Index];
    }
    if (Sum <= Avail) {
      break;
    }
    First++;
  }
  for (End = First; End < Count && Total + H[End] <= Avail; End++) {
    Total += H[End];
  }
  while (First > 0 && End == Count && Total + H[First - 1] <= Avail) {
    Total += H[--First];
  }
  *Scroll     = First;
  List->First = First;
  List->End   = End;
}

// Returns row Index's place. Rows from First to Index must show.
STATIC RECT
ListRow (
  IN CONST LIST *List,
  IN UINTN       Index
)
{
  INT32 Y = List->Area.Y + List->Margin;

  for (UINTN Row = List->First; Row < Index; Row++) {
    Y += List->Heights[Row];
  }
  return Rect (List->Area.X, Y, List->Area.W, List->Heights[Index]);
}

// The chevrons show there is more above or below. An empty list shows both.
STATIC VOID
ListChevrons (IN CONST LIST *List)
{
  RECT Area = List->Area;

  if (!List->Count || List->First > 0) {
    Icon (
      ICON_CHEVRON_UP, CenterX (Area), (float)(Area.Y + List->Margin / 2),
      ToPx (13), THEME_TEXT3
    );
  }
  if (!List->Count || List->End < List->Count) {
    Icon (
      ICON_CHEVRON_DOWN, CenterX (Area),
      (float)(Bottom (Area) - List->Margin / 2), ToPx (13), THEME_TEXT3
    );
  }
}

// Draws a list row.
STATIC VOID
RowItem (
  IN RECT         Row,
  IN ICON         RowIcon,
  IN CONST CHAR8 *Label,
  IN CONST CHAR8 *Value OPTIONAL,
  IN TONE         Tone,
  IN TRAILING     Trailing,
  IN BOOLEAN      Selected
)
{
  UINT32 Background = 0;
  UINT32 Edge       = THEME_LINE;
  UINT32 Foreground;
  UINT32 IconColor;
  UINT32 ValueColor;

  if (Tone == TONE_DANGER && Selected) {
    Background = THEME_DANGER_BG, Edge = THEME_DANGER_FILL,
    Foreground = THEME_DANGER_TEXT, IconColor = THEME_DANGER,
    ValueColor = THEME_DANGER_TEXT;
  } else if (Tone == TONE_DANGER) {
    Foreground = THEME_DANGER, IconColor = THEME_DANGER,
    ValueColor = THEME_TEXT2;
  } else if (Selected) {
    Background = THEME_ACCENT_BG, Edge = THEME_ACCENT,
    Foreground = THEME_ACCENT_TEXT, IconColor = THEME_ACCENT,
    ValueColor = THEME_ACCENT_TEXT;
  } else if (Tone == TONE_PRIMARY) {
    Foreground = THEME_TEXT, IconColor = THEME_ACCENT, ValueColor = THEME_TEXT2;
  } else {
    Foreground = THEME_TEXT, IconColor = THEME_TEXT2, ValueColor = THEME_TEXT2;
  }
  if (Selected) {
    RoundRect (Row, 8, Background);
    Ring (Row, 8, 1.5f, Edge);
  }

  float X   = (float)Row.X;
  float W   = (float)Row.W;
  float Pad = ToPx (10);
  float Cy  = CenterY (Row);
  Icon (RowIcon, X + Pad + ToPx (7.5f), Cy, ToPx (15), IconColor);
  float RightX = W - Pad;
  if (Trailing != TRAILING_NONE) {
    Icon (
      Trailing == TRAILING_CYCLE ? ICON_SELECTOR : ICON_CHEVRON_RIGHT,
      X + RightX - ToPx (6.5f), Cy, ToPx (13),
      Selected ? THEME_ACCENT : THEME_TEXT3
    );
    RightX -= ToPx (13 + 8);
  }
  float TextX = Pad + ToPx (25);
  if (Value && *Value) {
    float ValueW = Measure (Value, 12.5f, FONT_WEIGHT_REGULAR);
    ValueW       = CeilF (ValueW < W * 0.45f ? ValueW : W * 0.45f) + 1;
    TextIn (
      Rectf (X + RightX - ValueW, (float)Row.Y, ValueW, (float)Row.H), Value,
      12.5f, FONT_WEIGHT_REGULAR, ValueColor, ALIGN_RIGHT
    );
    RightX -= ValueW + ToPx (8);
  }
  float LabelW = RightX - TextX;
  TextIn (
    Rectf (X + TextX, (float)Row.Y, LabelW > 0 ? LabelW : 0, (float)Row.H),
    Label, 14.5f, FONT_WEIGHT_REGULAR, Foreground, ALIGN_LEFT
  );
}

// Header and footer

// Returns the width of the "OpenABL" wordmark at Size.
STATIC float
WordmarkWidth (IN float Size)
{
  return Measure ("Open", Size, FONT_WEIGHT_MEDIUM) +
         Measure ("ABL", Size, FONT_WEIGHT_MEDIUM);
}

// Draws the "OpenABL" wordmark from X, its capitals centred on Cy: "Open"
// in the text colour, "ABL" in the brand colour.
STATIC VOID
Wordmark (
  IN float X,
  IN float Cy,
  IN float Size
)
{
  float OpenW = Measure ("Open", Size, FONT_WEIGHT_MEDIUM);
  float AblW  = Measure ("ABL", Size, FONT_WEIGHT_MEDIUM);
  float H     = ToPx (Size) * 2;

  TextIn (
    Rectf (X, Cy - H / 2, OpenW + ToPx (2), H), "Open", Size,
    FONT_WEIGHT_MEDIUM, THEME_TEXT, ALIGN_LEFT
  );
  TextIn (
    Rectf (X + OpenW, Cy - H / 2, AblW + ToPx (2), H), "ABL", Size,
    FONT_WEIGHT_MEDIUM, THEME_BRAND, ALIGN_LEFT
  );
}

// The wordmark and the version in a chip at the right.
STATIC VOID
HeaderDraw (IN CONST APP *A)
{
  RECT  R           = mView.Header;
  CHAR8 Version[48] = "v";

  Wordmark ((float)R.X, CenterY (R), 20);
  AsciiStrAppend (Version, sizeof (Version), A->Version);
  float ChipW = Measure (Version, 12.5f, FONT_WEIGHT_REGULAR) + ToPx (20);
  float ChipH = ToPx (24);
  RECT  Chip =
    Rectf ((float)Right (R) - ChipW, CenterY (R) - ChipH / 2, ChipW, ChipH);
  Ring (Chip, 12, 1, THEME_LINE);
  TextIn (Chip, Version, 12.5f, FONT_WEIGHT_REGULAR, THEME_TEXT2, ALIGN_CENTER);
}

// The key hints, each an icon for the key and what it does. Handheld
// keypads have no back key and don't report holds, so every screen has its
// own way out (a Back row, Cancel, any key); on the log the power key goes
// back.
STATIC VOID
FooterDraw (IN CONST APP *A)
{
  RECT R = mView.Footer;
  typedef struct {
    ICON         Icon;
    CONST CHAR8 *Label;
  } HINT;
  BOOLEAN Handheld = A->Scheme == SCHEME_HANDHELD;
  BOOLEAN Back     = A->Screen == SCREEN_LINES;
  HINT    Hints[3] = {{ICON_ARROWS_VERTICAL, gStrings->HintMove}};
  UINTN   Count    = 1;
  float   IconW    = ToPx (13 + 5);
  float   Gap      = ToPx (18);
  float   Widths[3];
  float   Total = 0;

  if (!Back) {
    Hints[Count++] =
      (HINT){Handheld ? ICON_POWER : ICON_ENTER, gStrings->HintSelect};
  }
  if (Back || !Handheld) {
    Hints[Count++] =
      (HINT){Handheld ? ICON_POWER : ICON_ARROW_BACK, gStrings->HintBack};
  }

  Fill (Rect (R.X, R.Y, R.W, Imax (ToPxInt (1), 1)), THEME_LINE, 255);
  for (UINTN Index = 0; Index < Count; Index++) {
    Widths[Index] =
      Measure (Hints[Index].Label, 12.5f, FONT_WEIGHT_REGULAR) + 1;
    Total += Widths[Index] + IconW;
  }
  Total += Gap * (float)(Count - 1);
  float X = CenterX (R) - Total / 2;
  for (UINTN Index = 0; Index < Count; Index++) {
    Icon (
      Hints[Index].Icon, X + ToPx (6.5f), CenterY (R), ToPx (13), THEME_TEXT2
    );
    X += IconW;
    TextIn (
      Rectf (X, (float)R.Y, Widths[Index], (float)R.H), Hints[Index].Label,
      12.5f, FONT_WEIGHT_REGULAR, THEME_TEXT3, ALIGN_LEFT
    );
    X += Widths[Index] + Gap;
  }
}

// Menu

// Sizes InfoValue shrank values to, kept until the layout changes.
typedef struct _FIT {
  UINT32 Key;
  float  Size;
} FIT;

STATIC FIT   mFits[16];
STATIC UINTN mFitCount;

// Returns the size that fits String in W, down to 8.5, from Size where it
// is Width wide.
STATIC float
FitSize (
  IN CONST CHAR8 *String,
  IN float        Size,
  IN INT32        W,
  IN float        Width
)
{
  UINT32 Key =
    DigestStr (Digest (Digest (2166136261u, &W, 4), &Size, 4), String);

  for (UINTN Index = 0; Index < mFitCount; Index++) {
    if (mFits[Index].Key == Key) {
      return mFits[Index].Size;
    }
  }
  Size = FloorF (Size * (float)W / Width * 10) / 10;
  // Advances are whole pixels, so step down until it fits.
  while (Size > 8.5f &&
         Measure (String, Size, FONT_WEIGHT_REGULAR) > (float)W) {
    Size -= 0.1f;
  }
  if (Size < 8.5f) {
    Size = 8.5f;
  }
  mFits[mFitCount % 16] = (FIT){Key, Size};
  mFitCount++;
  if (mFitCount > 16) {
    mFitCount = 16;
  }
  return Size;
}

// Draws a value on the info card at Size if it fits. Otherwise it uses the
// size that just fits, down to 8.5 (enough for the longest model names on
// every layout), and only then cuts it short.
STATIC VOID
InfoValue (
  IN RECT         R,
  IN CONST CHAR8 *String,
  IN float        Size,
  IN UINT32       Rgb,
  IN BOOLEAN      RightAlign
)
{
  float Width = Measure (String, Size, FONT_WEIGHT_REGULAR);

  if (Width > (float)R.W) {
    Size = FitSize (String, Size, R.W, Width);
  }
  TextIn (
    R, String, Size, FONT_WEIGHT_REGULAR, Rgb,
    RightAlign ? ALIGN_RIGHT : ALIGN_LEFT
  );
}

typedef struct _INFO_ROW {
  ICON         Icon;
  CONST CHAR8 *Label;
  CHAR8        Value[MODEL_LEN + 16];
} INFO_ROW;

// Value, or None when it is empty.
STATIC CONST CHAR8 *
OrElse (
  IN CONST CHAR8 *Value,
  IN CONST CHAR8 *None
)
{
  return Value[0] ? Value : None;
}

STATIC VOID
InfoRows (
  IN CONST APP *A,
  OUT INFO_ROW  Out[6]
)
{
  CHAR8 Number[12];

  Out[0] = (INFO_ROW){.Icon = ICON_CPU, .Label = "SoC"};
  AsciiStrCopy (
    Out[0].Value, sizeof (Out[0].Value),
    OrElse (A->Info.Soc, gStrings->Unknown)
  );
  Out[1] = (INFO_ROW){.Icon = ICON_STACK, .Label = gStrings->InfoRam};
  AsciiStrCopy (
    Out[1].Value, sizeof (Out[1].Value),
    OrElse (A->Info.Ram, gStrings->Unknown)
  );
  Out[2] = (INFO_ROW){.Icon = ICON_DATABASE, .Label = gStrings->InfoStorage};
  AsciiStrCopy (
    Out[2].Value, sizeof (Out[2].Value),
    OrElse (A->Info.Storage, gStrings->Unknown)
  );
  Out[3] = (INFO_ROW){.Icon = ICON_SD_CARD, .Label = gStrings->SdCard};
  AsciiStrCopy (
    Out[3].Value, sizeof (Out[3].Value),
    OrElse (A->Info.SdCard, gStrings->NotInserted)
  );
  Out[4] = (INFO_ROW){.Icon = ICON_MONITOR, .Label = gStrings->InfoDisplay};
  AsciiStrFromU64 (Out[4].Value, sizeof (Out[4].Value), A->PhysicalW);
  AsciiStrAppend (Out[4].Value, sizeof (Out[4].Value), " × ");
  AsciiStrFromU64 (Number, sizeof (Number), A->PhysicalH);
  AsciiStrAppend (Out[4].Value, sizeof (Out[4].Value), Number);
  AsciiStrAppend (Out[4].Value, sizeof (Out[4].Value), ", ");
  AsciiStrFromU64 (Number, sizeof (Number), AppDesiredRotation (A));
  AsciiStrAppend (Out[4].Value, sizeof (Out[4].Value), Number);
  AsciiStrAppend (Out[4].Value, sizeof (Out[4].Value), "°");
  Out[5] = (INFO_ROW){.Icon = ICON_GAMEPAD, .Label = gStrings->InfoModel};
  AsciiStrCopy (
    Out[5].Value, sizeof (Out[5].Value),
    A->Settings.Model[0] ? A->Settings.Model : gStrings->NotSet
  );
}

// The info card beside the menu (landscape). Each row has its label and
// value on one line, or on two lines on a narrow screen.
STATIC VOID
InfoWide (
  IN CONST APP *A,
  IN RECT       R
)
{
  INFO_ROW Rows[6];
  RECT     Inner   = Inset (R, ToPxInt (12));
  BOOLEAN  TwoLine = (float)mView.W / mView.Unit < 560;
  INT32    Y       = Inner.Y;

  RoundRect (R, 12, THEME_CARD);
  InfoRows (A, Rows);
  if (!TwoLine) {
    TextIn (
      Rect (Inner.X, Y, Inner.W, ToPxInt (22)), gStrings->SectionSystem, 12.5f,
      FONT_WEIGHT_MEDIUM, THEME_TEXT3, ALIGN_LEFT
    );
    Y += ToPxInt (22);
  }
  for (INT32 Index = 0; Index < 6; Index++) {
    INT32 RowH = ToPxInt (TwoLine ? 36 : 30);
    if (Y + RowH > Bottom (Inner)) {
      break;
    }
    RECT Row = Rect (Inner.X, Y, Inner.W, RowH);
    Icon (
      Rows[Index].Icon, (float)Inner.X + ToPx (7), CenterY (Row), ToPx (14),
      THEME_TEXT3
    );
    INT32 TextX = Inner.X + ToPxInt (22);
    INT32 TextW = Right (Inner) - TextX;
    if (TwoLine) {
      TextIn (
        Rect (TextX, Y + ToPxInt (3), TextW, ToPxInt (15)), Rows[Index].Label,
        12.5f, FONT_WEIGHT_REGULAR, THEME_TEXT3, ALIGN_LEFT
      );
      InfoValue (
        Rect (TextX, Y + ToPxInt (18), TextW, ToPxInt (16)), Rows[Index].Value,
        13.5f, THEME_TEXT, FALSE
      );
    } else {
      INT32 LabelW = ToPxInt (62);
      TextIn (
        Rect (TextX, Y, LabelW, RowH), Rows[Index].Label, 12.5f,
        FONT_WEIGHT_REGULAR, THEME_TEXT2, ALIGN_LEFT
      );
      // Values line up at the right. A long one may use the row up to its
      // label.
      INT32 End = Imin (
        (INT32)Measure (Rows[Index].Label, 12.5f, FONT_WEIGHT_REGULAR) +
          ToPxInt (10),
        LabelW
      );
      InfoValue (
        Rect (TextX + End, Y, TextW - End, RowH), Rows[Index].Value, 13.5f,
        THEME_TEXT, TRUE
      );
    }
    Y += RowH;
  }
}

// The info card above the menu (portrait), in two columns.
STATIC VOID
InfoTall (
  IN CONST APP *A,
  IN RECT       R
)
{
  INFO_ROW Rows[6];
  RECT     Inner = Rect (
    R.X + ToPxInt (12), R.Y + ToPxInt (10), R.W - ToPxInt (24),
    R.H - ToPxInt (20)
  );
  INT32 Gap     = ToPxInt (12);
  INT32 ColumnW = (Inner.W - Gap) / 2;
  INT32 RowH    = Inner.H / 3;

  RoundRect (R, 12, THEME_CARD);
  InfoRows (A, Rows);
  for (INT32 Index = 0; Index < 6; Index++) {
    INT32 X = Inner.X + (Index % 2) * (ColumnW + Gap);
    INT32 Y = Inner.Y + (Index / 2) * RowH;
    TextIn (
      Rect (X, Y + RowH * 8 / 100, ColumnW, RowH * 42 / 100), Rows[Index].Label,
      12.5f, FONT_WEIGHT_REGULAR, THEME_TEXT3, ALIGN_LEFT
    );
    InfoValue (
      Rect (X, Y + RowH / 2, ColumnW, RowH * 45 / 100), Rows[Index].Value,
      13.5f, THEME_TEXT, FALSE
    );
  }
}

STATIC VOID
CountdownText (
  IN CONST APP *A,
  OUT CHAR8    *Out,
  IN UINTN      Cap
)
{
  UINT64       Seconds = 0;
  CHAR8        Number[12];
  CONST CHAR8 *What = A->Settings.BootMode == BOOT_MODE_ANDROID ? "Android"
                      : AppBootTitle (A) ? AppBootTitle (A)
                                         : "Linux";
  AppCountdownSeconds (A, &Seconds);
  AsciiStrFromU64 (Number, sizeof (Number), Seconds);
  CONST CHAR8 *Args[] = {What, Number};
  LangFill (Out, Cap, gStrings->Countdown, Args, 2);
}

// Returns the countdown bar's width out of Full. It shrinks to nothing
// over the time that was left when it first showed.
STATIC INT32
BarWidth (
  IN CONST APP *A,
  IN float      Full
)
{
  INT32 From;
  INT32 Step;

  if (!mView.BarShown || mView.BarUntil != A->CountdownUntil) {
    mView.BarShown = TRUE;
    mView.BarUntil = A->CountdownUntil;
    mView.BarTotal = (UINT64)A->Settings.MenuTimeoutS * 1000;
    if (mView.BarTotal == 0) {
      mView.BarTotal = 1;
    }
    mView.BarLeft = A->CountdownUntil > A->Now ? A->CountdownUntil - A->Now : 0;
    if (mView.BarLeft > mView.BarTotal) {
      mView.BarLeft = mView.BarTotal;
    }
    mView.BarTick = mView.Now;
  }
  From = (INT32)(Full * (float)mView.BarLeft / (float)mView.BarTotal);
  Step = Progress (mView.BarTick, (UINT32)mView.BarLeft);
  return From + ((Step * -From) >> 10);
}

STATIC VOID
Countdown (
  IN CONST APP *A,
  IN RECT       R
)
{
  float Pad = ToPx (10);
  CHAR8 Message[96];

  RoundRect (R, 8, THEME_ACCENT_BG);
  Icon (
    ICON_CLOCK, (float)R.X + Pad + ToPx (7.5f), CenterY (R), ToPx (15),
    THEME_ACCENT
  );
  CountdownText (A, Message, sizeof (Message));
  float TextX = (float)R.X + Pad + ToPx (25);
  float HintW =
    Measure (gStrings->AnyKeyToStay, 12.5f, FONT_WEIGHT_REGULAR) + 1;
  BOOLEAN FitsHint =
    Measure (Message, 13.5f, FONT_WEIGHT_MEDIUM) + HintW + ToPx (60) <
    (float)R.W;
  RECT TextR =
    Rectf (TextX, (float)R.Y, (float)Right (R) - TextX - Pad, (float)R.H);
  TextIn (
    TextR, Message, 13.5f, FONT_WEIGHT_MEDIUM, THEME_ACCENT_TEXT, ALIGN_LEFT
  );
  if (FitsHint) {
    TextIn (
      TextR, gStrings->AnyKeyToStay, 12.5f, FONT_WEIGHT_REGULAR, THEME_ACCENT,
      ALIGN_RIGHT
    );
  }
  float Full = (float)R.W - 2 * Pad;
  RECT  Bar  = Rectf (
    (float)R.X + Pad, (float)Bottom (R) - ToPx (3), Full,
    ToPx (1.5f) > 1 ? ToPx (1.5f) : 1
  );
  Bar.W = BarWidth (A, Full);
  Fill (Bar, THEME_ACCENT, 255);
}

// The menu, with the countdown banner over the list and the info card
// beside it (landscape) or above it (portrait).
STATIC VOID
MenuDraw (IN OUT APP *A)
{
  ROW     Rows[MAX_ROWS];
  INT32   Heights[MAX_ROWS];
  BOOLEAN Headers[MAX_ROWS];
  UINTN   Count = AppRows (A, Rows);
  UINTN   Focus = AppFocusedRow (A, Rows, Count);
  RECT    Body  = mView.Body;
  RECT    Area;
  RECT    Card;
  LIST    List;

  if (mView.W > mView.H) {
    INT32 Gap = ToPxInt (14);
    RECT  LeftPart;
    RECT  RightPart;
    TakeLeft (
      Body, (INT32)((float)(Body.W - Gap) * 0.6f), &LeftPart, &RightPart
    );
    Area = LeftPart;
    Card =
      Rect (RightPart.X + Gap, RightPart.Y, RightPart.W - Gap, RightPart.H);
  } else {
    RECT  TopPart;
    RECT  BottomPart;
    INT32 Gap = ToPxInt (10);
    TakeTop (Body, ToPxInt (128), &TopPart, &BottomPart);
    Area =
      Rect (BottomPart.X, BottomPart.Y + Gap, BottomPart.W, BottomPart.H - Gap);
    Card = TopPart;
  }
  if (A->Countdown) {
    RECT  Banner;
    RECT  Rest;
    INT32 Gap = ToPxInt (8);
    TakeTop (Area, ToPxInt (34), &Banner, &Rest);
    Countdown (A, Banner);
    Area = Rect (Rest.X, Rest.Y + Gap, Rest.W, Rest.H - Gap);
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    Headers[Index] = Rows[Index].Header != NULL;
    Heights[Index] = ToPxInt (Headers[Index] ? 22 : 30);
  }
  ListInit (&List, Area, ToPxInt (14), Count, Heights, Headers);
  ListPlace (&List, Focus, &A->Menu.Scroll);
  for (UINTN Index = List.First; Index < List.End; Index++) {
    RECT Row = ListRow (&List, Index);
    if (Rows[Index].Header) {
      TextIn (
        Rect (
          Row.X + ToPxInt (10), Row.Y + ToPxInt (8), Row.W - ToPxInt (10),
          Row.H - ToPxInt (8)
        ),
        Rows[Index].Header, 12.5f, FONT_WEIGHT_MEDIUM, THEME_TEXT3, ALIGN_LEFT
      );
    } else {
      ITEM_VIEW View;
      AppItemView (A, Rows[Index].Item, &View);
      RowItem (
        Row, View.Icon, View.Label, View.Value, View.Tone, View.Trailing,
        Index == Focus
      );
    }
  }
  ListChevrons (&List);
  if (mView.W > mView.H) {
    InfoWide (A, Card);
  } else {
    InfoTall (A, Card);
  }
}

// Model and entry lists

// A side card (landscape) with an info icon and String.
STATIC VOID
SideCard (
  IN RECT         Side,
  IN CONST CHAR8 *String
)
{
  RECT  Inner    = Inset (Side, ToPxInt (14));
  INT32 TextX    = Inner.X + ToPxInt (26);
  float TextW    = (float)(Right (Inner) - TextX);
  float LineH    = ToPx (13.5f * 1.45f);
  INT32 MaxLines = (INT32)((float)Inner.H / LineH);

  RoundRect (Side, 12, THEME_CARD);
  Icon (
    ICON_INFO, (float)Inner.X + ToPx (8), (float)Inner.Y + ToPx (9), ToPx (16),
    THEME_ACCENT
  );
  Wrapped (
    (float)TextX, (float)Inner.Y, TextW, String, 13.5f, FONT_WEIGHT_REGULAR,
    LineH, MaxLines < 1 ? 1 : MaxLines, THEME_TEXT2, ALIGN_LEFT
  );
}

// Splits Rest into the list and, in landscape, a side card beside it.
STATIC VOID
Split (
  IN RECT      Rest,
  OUT RECT    *List,
  OUT RECT    *Side,
  OUT BOOLEAN *HasSide
)
{
  *HasSide = mView.W > mView.H;
  if (*HasSide) {
    INT32 Gap = ToPxInt (14);
    RECT  LeftPart;
    RECT  RightPart;
    TakeLeft (
      Rest, (INT32)((float)(Rest.W - Gap) * 0.6f), &LeftPart, &RightPart
    );
    *List = LeftPart;
    *Side =
      Rect (RightPart.X + Gap, RightPart.Y, RightPart.W - Gap, RightPart.H);
  } else {
    *List = Rest;
  }
}

// Fills Heights with Count rows 30 units high.
STATIC CONST INT32 *
EvenRows (IN UINTN Count)
{
  STATIC INT32 Heights[MAX_MODELS + 2];

  for (UINTN Index = 0; Index < Count && Index < MAX_MODELS + 2; Index++) {
    Heights[Index] = ToPxInt (30);
  }
  return Heights;
}

STATIC VOID
ModelsSideText (
  IN CONST APP *A,
  OUT CHAR8    *Out,
  IN UINTN      Cap
)
{
  UINTN Focus = A->ModelList.Focus;

  if (Focus == 0) {
    AsciiStrCopy (Out, Cap, gStrings->FirmwareDtExplain);
    return;
  }
  AsciiStrCopy (Out, Cap, gStrings->ModelExplain);
  if (Focus - 1 < A->ModelCount) {
    CHAR8        Found[96];
    CONST CHAR8 *Source = A->Models[Focus - 1].Source;
    LangFill (Found, sizeof (Found), gStrings->FoundIn, &Source, 1);
    AsciiStrAppend (Out, Cap, Found);
  }
}

STATIC VOID
ModelsDraw (IN OUT APP *A)
{
  RECT    Title;
  RECT    Rest;
  RECT    Area;
  RECT    Side;
  LIST    List;
  CHAR8   Subtitle[64];
  CHAR8   Number[12];
  CHAR8   SideText[256];
  BOOLEAN HasSide;
  // Rows: "Firmware default", each model, "Back".
  UINTN Count = A->ModelCount + 2;
  UINTN Focus = A->ModelList.Focus < Count ? A->ModelList.Focus : Count - 1;

  TakeTop (mView.Body, ToPxInt (46), &Title, &Rest);
  TextIn (
    Rect (Title.X, Title.Y, Title.W, ToPxInt (26)), gStrings->DeviceModel, 18,
    FONT_WEIGHT_MEDIUM, THEME_TEXT, ALIGN_LEFT
  );
  if (A->ModelCount == 0) {
    AsciiStrCopy (Subtitle, sizeof (Subtitle), gStrings->NoDeviceTrees);
  } else if (A->ModelCount == 1) {
    AsciiStrCopy (Subtitle, sizeof (Subtitle), gStrings->OneModel);
  } else {
    CONST CHAR8 *Arg = Number;
    AsciiStrFromU64 (Number, sizeof (Number), A->ModelCount);
    LangFill (Subtitle, sizeof (Subtitle), gStrings->NModels, &Arg, 1);
  }
  TextIn (
    Rect (Title.X, Title.Y + ToPxInt (26), Title.W, ToPxInt (18)), Subtitle,
    12.5f, FONT_WEIGHT_REGULAR, THEME_TEXT2, ALIGN_LEFT
  );
  INT32 Gap = ToPxInt (6);
  Split (
    Rect (Rest.X, Rest.Y + Gap, Rest.W, Rest.H - Gap), &Area, &Side, &HasSide
  );
  ListInit (&List, Area, ToPxInt (14), Count, EvenRows (Count), NULL);
  ListPlace (&List, Focus, &A->ModelList.Scroll);
  for (UINTN Index = List.First; Index < List.End; Index++) {
    RECT Row = ListRow (&List, Index);
    if (Index == 0) {
      BOOLEAN Current = !A->Settings.Model[0];
      RowItem (
        Row, Current ? ICON_CIRCLE_CHECK : ICON_CIRCLE,
        gStrings->FirmwareDefault, Current ? gStrings->Current : NULL,
        TONE_NORMAL, TRAILING_NONE, Index == Focus
      );
    } else if (Index == Count - 1) {
      RowItem (
        Row, ICON_ARROW_LEFT, gStrings->Back, NULL, TONE_NORMAL, TRAILING_NONE,
        Index == Focus
      );
    } else {
      CONST MODEL *Model   = &A->Models[Index - 1];
      BOOLEAN      Current = AsciiStrEq (A->Settings.Model, Model->Name);
      RowItem (
        Row, Current ? ICON_CIRCLE_CHECK : ICON_CIRCLE, Model->Name,
        Current ? gStrings->Current : NULL, TONE_NORMAL, TRAILING_NONE,
        Index == Focus
      );
    }
  }
  ListChevrons (&List);
  if (HasSide) {
    ModelsSideText (A, SideText, sizeof (SideText));
    SideCard (Side, SideText);
  }
}

// Returns how many lines the subtitle takes. Portrait has no side card, so
// the focused entry's details go under the title, on up to two lines.
STATIC INT32
EntriesSubLines (IN CONST APP *A)
{
  UINTN Focus = A->EntryList.Focus;

  if (Focus < A->EntryCount && mView.W <= mView.H) {
    INT32 Lines = WrapCount (
      A->Entries[Focus].Detail, 12.5f, FONT_WEIGHT_REGULAR, (float)mView.Body.W
    );
    return Lines < 1 ? 1 : Lines > 2 ? 2 : Lines;
  }
  return 1;
}

STATIC VOID
EntriesSideText (
  IN CONST APP *A,
  OUT CHAR8    *Out,
  IN UINTN      Cap
)
{
  UINTN Focus = A->EntryList.Focus;

  if (Focus < A->EntryCount) {
    AsciiStrCopy (Out, Cap, A->Entries[Focus].Title);
    AsciiStrAppend (Out, Cap, ". ");
    AsciiStrAppend (Out, Cap, A->Entries[Focus].Detail);
  } else {
    AsciiStrCopy (Out, Cap, gStrings->BackToMenu);
  }
}

STATIC VOID
EntriesDraw (IN OUT APP *A)
{
  RECT    Title;
  RECT    Rest;
  RECT    Area;
  RECT    Side;
  LIST    List;
  BOOLEAN HasSide;
  CHAR8   SideText[320];
  // Rows: each entry, "Back".
  UINTN Count = A->EntryCount + 1;
  UINTN Focus = A->EntryList.Focus < Count ? A->EntryList.Focus : Count - 1;
  INT32 Lines = EntriesSubLines (A);

  TakeTop (mView.Body, ToPxInt (28 + 18 * (float)Lines), &Title, &Rest);
  TextIn (
    Rect (Title.X, Title.Y, Title.W, ToPxInt (26)), gStrings->BootEntries, 18,
    FONT_WEIGHT_MEDIUM, THEME_TEXT, ALIGN_LEFT
  );
  CONST CHAR8 *Subtitle =
    A->EntryList.Focus < A->EntryCount && mView.W <= mView.H
      ? A->Entries[A->EntryList.Focus].Detail
      : gStrings->EntriesExplain;
  Wrapped (
    (float)Title.X, (float)Title.Y + ToPx (26), (float)Title.W, Subtitle, 12.5f,
    FONT_WEIGHT_REGULAR, ToPx (18), 2, THEME_TEXT2, ALIGN_LEFT
  );
  INT32 Gap = ToPxInt (6);
  Split (
    Rect (Rest.X, Rest.Y + Gap, Rest.W, Rest.H - Gap), &Area, &Side, &HasSide
  );
  ListInit (&List, Area, ToPxInt (14), Count, EvenRows (Count), NULL);
  ListPlace (&List, Focus, &A->EntryList.Scroll);
  for (UINTN Index = List.First; Index < List.End; Index++) {
    RECT Row = ListRow (&List, Index);
    if (Index < A->EntryCount) {
      CONST ENTRY_VIEW *Entry = &A->Entries[Index];
      if (Entry->Bad) {
        RowItem (
          Row, ICON_ALERT, Entry->Title, gStrings->NoTriesLeft, TONE_DANGER,
          TRAILING_NONE, Index == Focus
        );
      } else {
        RowItem (
          Row, Index == 0 ? ICON_PLAY : ICON_CIRCLE, Entry->Title,
          Index == 0 ? gStrings->Default : NULL, TONE_NORMAL, TRAILING_NONE,
          Index == Focus
        );
      }
    } else {
      RowItem (
        Row, ICON_ARROW_LEFT, gStrings->Back, NULL, TONE_NORMAL, TRAILING_NONE,
        Index == Focus
      );
    }
  }
  ListChevrons (&List);
  if (HasSide) {
    EntriesSideText (A, SideText, sizeof (SideText));
    SideCard (Side, SideText);
  }
}

// Device info and the log

// Row heights, worked out again only when the lines or the layout change.
STATIC struct {
  BOOLEAN Valid;
  UINT32  Key;
  INT32   Heights[MAX_LINES];
} mLines;

STATIC CONST INT32 *
LinesHeights (
  IN CONST APP *A,
  IN UINTN      Count,
  IN float      Size,
  IN float      LineH,
  IN INT32      Width
)
{
  UINT32 Key = Digest (2166136261u, &Count, sizeof (Count));

  Key = Digest (Key, &Size, sizeof (Size));
  Key = Digest (Key, &Width, sizeof (Width));
  for (UINTN Index = 0; Index < Count; Index++) {
    Key =
      DigestStr (DigestStr (Key, A->Lines[Index].Label), A->Lines[Index].Value);
  }
  if (mLines.Valid && mLines.Key == Key) {
    return mLines.Heights;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    CONST TEXT_LINE *Line = &A->Lines[Index];
    INT32            Lines =
      WrapCount (Line->Value, Size, FONT_WEIGHT_REGULAR, (float)Width);
    Lines        = Lines < 1 ? 1 : Lines > 3 ? 3 : Lines;
    INT32 LabelH = Line->Label[0] ? ToPxInt (22) : ToPxInt (4);
    mLines.Heights[Index] =
      LabelH + (INT32)CeilF (LineH * (float)Lines) + ToPxInt (6);
  }
  mLines.Valid = TRUE;
  mLines.Key   = Key;
  return mLines.Heights;
}

// One row per line, with its label (if any) over the value, wrapped to up
// to three lines. The focused row gets a card behind it.
STATIC VOID
LinesDraw (IN OUT APP *A)
{
  RECT    Title;
  RECT    Area;
  LIST    List;
  BOOLEAN Log   = A->LinesFrom == ITEM_LOG;
  float   Size  = Log ? 12.5f : 13.5f;
  float   LineH = ToPx (Log ? 12.5f * 1.3f : 13.5f * 1.4f);
  INT32   Pad   = ToPxInt (10);
  UINTN   Count = A->LineCount < MAX_LIST ? A->LineCount : MAX_LIST;
  UINTN   Focus = A->InfoList.Focus;

  TakeTop (mView.Body, ToPxInt (34), &Title, &Area);
  TextIn (
    Rect (Title.X, Title.Y, Title.W, ToPxInt (26)), A->LinesTitle, 18,
    FONT_WEIGHT_MEDIUM, THEME_TEXT, ALIGN_LEFT
  );
  INT32 Width = Area.W - 2 * Pad;
  ListInit (
    &List, Area, ToPxInt (12), Count,
    LinesHeights (A, Count, Size, LineH, Width), NULL
  );
  if (Count && Focus >= Count) {
    Focus = Count - 1;
  }
  ListPlace (&List, Focus, &A->InfoList.Scroll);
  for (UINTN Index = List.First; Index < List.End; Index++) {
    CONST TEXT_LINE *Line   = &A->Lines[Index];
    RECT             Row    = ListRow (&List, Index);
    INT32            LabelH = Line->Label[0] ? ToPxInt (22) : ToPxInt (4);
    if (Index == Focus) {
      RoundRect (Row, 8, THEME_CARD);
    }
    if (Line->Label[0]) {
      TextIn (
        Rect (Row.X + Pad, Row.Y + ToPxInt (4), Row.W - 2 * Pad, ToPxInt (16)),
        Line->Label, 12.5f, FONT_WEIGHT_REGULAR, THEME_TEXT3, ALIGN_LEFT
      );
    }
    Wrapped (
      (float)(Row.X + Pad), (float)(Row.Y + LabelH), (float)Width, Line->Value,
      Size, FONT_WEIGHT_REGULAR, LineH, 3, THEME_TEXT, ALIGN_LEFT
    );
  }
  ListChevrons (&List);
}

// Screens without a list

// Draws String centred on Cx from *Y down, wrapped to the screen's width,
// and moves *Y past it.
STATIC VOID
CentredLines (
  IN float        Cx,
  IN OUT float   *Y,
  IN CONST CHAR8 *String,
  IN float        Size,
  IN FONT_WEIGHT  Weight,
  IN UINT32       Rgb
)
{
  float LineH = ToPx (Size * 1.45f);
  float Width = (float)mView.W;
  INT32 Lines = WrapCount (String, Size, Weight, Width);

  Wrapped (
    Cx - Width / 2, *Y, Width, String, Size, Weight, LineH, 8, Rgb, ALIGN_CENTER
  );
  *Y += LineH * (float)(Lines < 1 ? 1 : Lines);
}

STATIC VOID
ScanningDraw (VOID)
{
  float Cx = CenterX (mView.Body);
  float Y  = CenterY (mView.Body) - ToPx (40);

  Icon (ICON_DATABASE, Cx, Y + ToPx (14), ToPx (28), THEME_ACCENT);
  Y += ToPx (40);
  CentredLines (
    Cx, &Y, gStrings->LookingForDts, 15, FONT_WEIGHT_MEDIUM, THEME_TEXT
  );
  CentredLines (
    Cx, &Y, gStrings->FewSeconds, 12.5f, FONT_WEIGHT_REGULAR, THEME_TEXT2
  );
}

// While the uninstall rewrites the partition table.
STATIC VOID
UninstallingDraw (VOID)
{
  float Cx = CenterX (mView.Body);
  float Y  = CenterY (mView.Body) - ToPx (40);

  Icon (ICON_TRASH, Cx, Y + ToPx (14), ToPx (28), THEME_DANGER);
  Y += ToPx (40);
  CentredLines (
    Cx, &Y, gStrings->Uninstalling, 15, FONT_WEIGHT_MEDIUM, THEME_TEXT
  );
  CentredLines (
    Cx, &Y, gStrings->DontTurnOff, 12.5f, FONT_WEIGHT_REGULAR, THEME_TEXT2
  );
}

// After an uninstall, until the platform restarts into recovery.
STATIC VOID
RestartingDraw (IN CONST APP *A)
{
  float Cx = CenterX (mView.Body);
  float Y  = CenterY (mView.Body) - ToPx (40);

  Icon (ICON_LIFEBUOY, Cx, Y + ToPx (14), ToPx (28), THEME_ACCENT);
  Y += ToPx (40);
  CentredLines (
    Cx, &Y, gStrings->RestartingRecovery, 15, FONT_WEIGHT_MEDIUM, THEME_TEXT
  );
  CentredLines (
    Cx, &Y, A->Wiped ? gStrings->CfwRemoved : gStrings->WipeInRecovery, 12.5f,
    FONT_WEIGHT_REGULAR, THEME_TEXT2
  );
}

STATIC VOID
BootingDraw (IN CONST APP *A)
{
  float        Cx   = CenterX (mView.Body);
  float        CapH = FontCapHeight (ToPx (40));
  float        Y    = CenterY (mView.Body) - ToPx (60);
  CHAR8        What[160];
  CHAR8        Detail[192];
  CONST CHAR8 *Title = AppBootTitle (A);

  Wordmark (Cx - WordmarkWidth (40) / 2, Y + CapH / 2, 40);
  Y += CapH + ToPx (18);
  if (A->Settings.BootMode == BOOT_MODE_ANDROID) {
    AsciiStrCopy (What, sizeof (What), gStrings->StartingAndroid);
  } else if (Title) {
    LangFill (What, sizeof (What), gStrings->Starting, &Title, 1);
  } else {
    AsciiStrCopy (What, sizeof (What), gStrings->StartingLinux);
  }
  CentredLines (Cx, &Y, What, 15, FONT_WEIGHT_MEDIUM, THEME_TEXT);
  AppBootDetail (A, Detail, sizeof (Detail));
  CentredLines (Cx, &Y, Detail, 12.5f, FONT_WEIGHT_REGULAR, THEME_TEXT2);
  Y += ToPx (10);

  // A segment sweeps along the track while the boot starts. It starts half
  // way, on the track: the boot often begins before the next frame, and the
  // panel keeps this one.
  if (!mView.Sweeping) {
    mView.Sweeping  = TRUE;
    mView.SweepTick = mView.Now - SWEEP_MS / 2;
  }
  float  TrackW  = ToPx (200);
  RECT   Track   = Rectf (Cx - TrackW / 2, Y, TrackW, ToPx (4));
  RECT   Segment = Rectf (TrackW * 0.3f, 0, TrackW * 0.3f, ToPx (4));
  INT32  From    = (INT32)(-TrackW * 0.3f);
  INT32  To      = (INT32)TrackW;
  UINT32 Phase   = (mView.Now - mView.SweepTick) % SWEEP_MS;
  float  Eased   = EaseInOut ((float)Phase / SWEEP_MS);

  mView.Animating = TRUE;
  RoundRect (Track, 2, THEME_LINE);
  Segment.X = Track.X + From + (INT32)((float)(To - From) * Eased);
  Segment.Y += Track.Y;
  mu_push_clip_rect (mView.Ctx, mu_rect (Track.X, Track.Y, Track.W, Track.H));
  RoundRect (Segment, 2, THEME_ACCENT);
  mu_pop_clip_rect (mView.Ctx);
}

// The error screen: what went wrong, a hint and the key to go on.
STATIC VOID
ErrorDraw (IN CONST APP *A)
{
  CONST CHAR8 *Title, *Message, *Hint, *Action;

  switch (A->Error) {
  case BOOT_ERROR_NO_IMAGE:
    Title = gStrings->ErrNoImageTitle, Message = gStrings->ErrNoImageMsg,
    Hint   = gStrings->ErrNoImageHint;
    Action = gStrings->PressAnyKeyMenu;
    break;
  case BOOT_ERROR_LOAD_FAILED:
    Title = gStrings->ErrLoadTitle, Message = A->ErrorDetail,
    Hint   = gStrings->ErrLoadHint;
    Action = gStrings->PressAnyKeyMenu;
    break;
  case BOOT_ERROR_ANDROID:
    Title = gStrings->ErrAndroidTitle, Message = gStrings->ErrAndroidMsg,
    Hint   = gStrings->ErrAndroidHint;
    Action = gStrings->PressAnyKeyMenu;
    break;
  case BOOT_ERROR_ANDROID_FAILED:
    Title   = gStrings->ErrAndroidFailedTitle,
    Message = gStrings->ErrAndroidFailedMsg,
    Hint    = gStrings->ErrAndroidFailedHint;
    Action  = gStrings->PressAnyKeyMenu;
    break;
  case BOOT_ERROR_WIPE_BY_HAND:
    Title = gStrings->ErrWipeTitle, Message = gStrings->WipeByHand, Hint = "";
    Action = gStrings->PressAnyKeyMenu;
    break;
  case BOOT_ERROR_UNINSTALL:
    Title = gStrings->UninstallFailed, Message = A->ErrorDetail, Hint = "";
    Action = gStrings->PressAnyKeyMenu;
    break;
  default:
    Title = gStrings->ErrModelTitle, Message = gStrings->ErrModelMsg, Hint = "";
    Action = gStrings->PressAnyKeyModel;
    break;
  }
  float MaxW =
    ToPx (300) < (float)mView.Body.W ? ToPx (300) : (float)mView.Body.W;
  INT32 TitleLines   = WrapCount (Title, 20, FONT_WEIGHT_MEDIUM, MaxW);
  INT32 MessageLines = WrapCount (Message, 14.5f, FONT_WEIGHT_REGULAR, MaxW);
  INT32 HintLines    = WrapCount (Hint, 13.5f, FONT_WEIGHT_REGULAR, MaxW);
  float Total        = ToPx (56 + 14) + (float)TitleLines * ToPx (20 * 1.45f) +
                       (float)MessageLines * ToPx (14.5f * 1.45f) +
                       (float)HintLines * ToPx (13.5f * 1.45f) + ToPx (22 + 32);
  float Cx           = CenterX (mView.Body);
  float Y            = CenterY (mView.Body) - Total / 2;

  Disc (Cx, Y + ToPx (28), ToPx (28), THEME_DANGER_BG);
  Icon (ICON_ALERT, Cx, Y + ToPx (28), ToPx (28), THEME_DANGER);
  Y += ToPx (56 + 14);
  Wrapped (
    Cx - MaxW / 2, Y, MaxW, Title, 20, FONT_WEIGHT_MEDIUM, ToPx (20 * 1.45f), 8,
    THEME_TEXT, ALIGN_CENTER
  );
  Y += (float)TitleLines * ToPx (20 * 1.45f);
  Wrapped (
    Cx - MaxW / 2, Y, MaxW, Message, 14.5f, FONT_WEIGHT_REGULAR,
    ToPx (14.5f * 1.45f), 8, THEME_TEXT2, ALIGN_CENTER
  );
  Y += (float)MessageLines * ToPx (14.5f * 1.45f);
  if (HintLines) {
    Wrapped (
      Cx - MaxW / 2, Y, MaxW, Hint, 13.5f, FONT_WEIGHT_REGULAR,
      ToPx (13.5f * 1.45f), 8, THEME_TEXT3, ALIGN_CENTER
    );
  }
  Y += (float)HintLines * ToPx (13.5f * 1.45f) + ToPx (22);
  float ActionW = Measure (Action, 13.5f, FONT_WEIGHT_REGULAR) + 1;
  float PillW   = ActionW + ToPx (14 + 8 + 28);
  if (PillW > (float)mView.Body.W) {
    PillW = (float)mView.Body.W;
  }
  RECT Pill = Rectf (Cx - PillW / 2, Y, PillW, ToPx (32));
  RoundRect (Pill, 16, THEME_CARD);
  Icon (
    A->Scheme == SCHEME_HANDHELD ? ICON_POWER : ICON_ENTER,
    (float)Pill.X + ToPx (14 + 7), CenterY (Pill), ToPx (14), THEME_ACCENT
  );
  TextIn (
    Rectf (
      (float)Pill.X + ToPx (14 + 14 + 8), (float)Pill.Y, ActionW, (float)Pill.H
    ),
    Action, 13.5f, FONT_WEIGHT_REGULAR, THEME_TEXT, ALIGN_LEFT
  );
}

// Confirmation and toast, drawn over everything

// A partition's size for the uninstall card: whole GiB, or MiB below one.
STATIC VOID
SizeText (
  IN UINT64  Bytes,
  OUT CHAR8 *Out,
  IN UINTN   OutSize
)
{
  if (Bytes >= (1ULL << 30)) {
    SPrint (Out, OutSize, "%llu GiB", Bytes >> 30);
  } else {
    SPrint (Out, OutSize, "%llu MiB", Bytes >> 20);
  }
}

// The confirmation card over a dimmed screen. The selected button is filled
// (red for an uninstall) and gets a ring unless it is red. The first
// uninstall card lists the partitions it erases, a row each.
STATIC VOID
OverlayDraw (IN CONST APP *A)
{
  BOOLEAN      Final  = A->Confirm == CONFIRM_UNINSTALL_FINAL;
  BOOLEAN      Danger = A->Confirm == CONFIRM_UNINSTALL || Final;
  ICON         CardIcon = Danger ? ICON_TRASH : ICON_CPU;
  CONST CHAR8 *Title   = Final  ? gStrings->ConfirmUninstallFinalTitle
                         : Danger ? gStrings->ConfirmUninstallTitle
                                  : gStrings->ConfirmEdlTitle;
  CONST CHAR8 *Message = Final  ? gStrings->ConfirmUninstallFinalMsg
                         : Danger ? gStrings->ConfirmUninstallMsg
                                  : gStrings->ConfirmEdlMsg;
  CONST CHAR8 *Confirm = Final  ? gStrings->ConfirmUninstallFinal
                         : Danger ? gStrings->ConfirmUninstall
                                  : gStrings->ConfirmEdl;
  UINTN        Parts =
    A->Confirm == CONFIRM_UNINSTALL ? A->CfwPartCount : 0;

  Fill (Rect (0, 0, mView.W, mView.H), 0x000000, 158);
  float CardW =
    ToPx (300) < (float)mView.W * 0.82f ? ToPx (300) : (float)mView.W * 0.82f;
  float Pad          = ToPx (18);
  float TextW        = CardW - 2 * Pad;
  float RowH         = ToPx (13.5f * 1.45f);
  INT32 TitleLines   = WrapCount (Title, 18, FONT_WEIGHT_MEDIUM, TextW);
  INT32 MessageLines = WrapCount (Message, 13.5f, FONT_WEIGHT_REGULAR, TextW);
  float CardH = 2 * Pad + ToPx (40 + 12) +
                (float)TitleLines * ToPx (18 * 1.3f) + ToPx (6) +
                (float)MessageLines * RowH +
                (Parts ? ToPx (6) + (float)Parts * RowH : 0) + ToPx (16 + 34);
  RECT  Card  = Rectf (
    (float)mView.W / 2 - CardW / 2, (float)mView.H / 2 - CardH / 2, CardW, CardH
  );
  RoundRect (Card, 16, THEME_RAISED);
  Ring (Card, 16, 1, THEME_LINE);
  float X = (float)Card.X + Pad;
  float Y = (float)Card.Y + Pad;
  Disc (
    X + ToPx (20), Y + ToPx (20), ToPx (20),
    Danger ? THEME_DANGER_BG : THEME_ACCENT_BG
  );
  Icon (
    CardIcon, X + ToPx (20), Y + ToPx (20), ToPx (20),
    Danger ? THEME_DANGER : THEME_ACCENT
  );
  Y += ToPx (40 + 12);
  Wrapped (
    X, Y, TextW, Title, 18, FONT_WEIGHT_MEDIUM, ToPx (18 * 1.3f), 8, THEME_TEXT,
    ALIGN_LEFT
  );
  Y += (float)TitleLines * ToPx (18 * 1.3f) + ToPx (6);
  Wrapped (
    X, Y, TextW, Message, 13.5f, FONT_WEIGHT_REGULAR, RowH, 12,
    THEME_TEXT2, ALIGN_LEFT
  );
  Y += (float)MessageLines * RowH;
  if (Parts) {
    Y += ToPx (6);
    for (UINTN Index = 0; Index < Parts; Index++) {
      CHAR8 Size[24];
      float SizeW;

      SizeText (A->CfwParts[Index].Bytes, Size, sizeof (Size));
      SizeW = Measure (Size, 13.5f, FONT_WEIGHT_REGULAR);
      TextIn (
        Rectf (X, Y, TextW, RowH), Size, 13.5f, FONT_WEIGHT_REGULAR,
        THEME_DANGER, ALIGN_RIGHT
      );
      TextIn (
        Rectf (X, Y, TextW - SizeW - ToPx (8), RowH), A->CfwParts[Index].Name,
        13.5f, FONT_WEIGHT_MEDIUM, THEME_DANGER, ALIGN_LEFT
      );
      Y += RowH;
    }
  }
  Y += ToPx (16);
  float        Gap       = ToPx (10);
  float        ButtonW   = (TextW - Gap) / 2;
  float        ButtonH   = ToPx (34);
  CONST CHAR8 *Labels[2] = {gStrings->Cancel, Confirm};
  for (INT32 Index = 0; Index < 2; Index++) {
    RECT Button =
      Rectf (X + (float)Index * (ButtonW + Gap), Y, ButtonW, ButtonH);
    BOOLEAN IsConfirm = Index == 1;
    BOOLEAN Selected  = IsConfirm == A->ConfirmYes;
    UINT32  Background;
    UINT32  Foreground;
    if (IsConfirm && Danger) {
      Background = Selected ? THEME_DANGER_FILL : THEME_BUTTON,
      Foreground = Selected ? THEME_DANGER_TEXT : THEME_DANGER;
    } else {
      Background = Selected ? THEME_ACCENT_BG : THEME_BUTTON,
      Foreground = Selected ? THEME_ACCENT_TEXT : THEME_TEXT;
    }
    RoundRect (Button, 8, Background);
    if (Selected && !(IsConfirm && Danger)) {
      Ring (Button, 8, 1.5f, THEME_ACCENT);
    }
    TextIn (
      Button, Labels[Index], 14.5f, FONT_WEIGHT_REGULAR, Foreground,
      ALIGN_CENTER
    );
  }
}

STATIC BOOLEAN
HasFooter (IN CONST APP *A)
{
  return A->Screen == SCREEN_MENU || A->Screen == SCREEN_MODELS ||
         A->Screen == SCREEN_ENTRIES || A->Screen == SCREEN_CONFIRM ||
         A->Screen == SCREEN_LINES;
}

// The toast sits above the footer, or above the body's end on a screen
// without one.
STATIC VOID
ToastDraw (IN CONST APP *A)
{
  float H     = ToPx (32);
  float Top   = (float)(HasFooter (A) ? mView.Footer.Y : Bottom (mView.Body)) -
                H - ToPx (8);
  float TextW = Measure (A->Toast, 13.5f, FONT_WEIGHT_REGULAR) + 1;
  float MaxW  = (float)mView.W - ToPx (28);
  float W     = TextW + ToPx (14 * 2 + 14 + 8);

  if (W > MaxW) {
    W = MaxW;
  }
  RECT Area = Rectf ((float)mView.W / 2 - W / 2, Top, W, H);
  RoundRect (Area, 16, THEME_TOAST);
  Icon (
    ICON_INFO, (float)Area.X + ToPx (14 + 7), (float)Area.Y + (float)Area.H / 2,
    ToPx (14), THEME_ACCENT
  );
  float TextX = ToPx (14 + 14 + 8);
  TextIn (
    Rectf (
      (float)Area.X + TextX, (float)Area.Y, (float)Area.W - TextX - ToPx (14),
      (float)Area.H
    ),
    A->Toast, 13.5f, FONT_WEIGHT_REGULAR, THEME_TEXT, ALIGN_LEFT
  );
}

// Frames

// Lays out the header, body and footer for the picture's size, and starts
// the caches over, since their sizes change with the screen's.
STATIC VOID
Layout (IN CONST APP *A)
{
  RECT  Full = Rect (0, 0, mView.W, mView.H);
  RECT  Area;
  RECT  Rest;
  INT32 Gap;

  mView.Unit = (float)Imin (mView.W, mView.H) / 360.0f;
  Gap        = ToPxInt (10);
  Area       = Inset (Full, ToPxInt (14));
  TakeTop (Area, ToPxInt (36), &mView.Header, &Rest);
  Rest = Rect (Rest.X, Rest.Y + Gap, Rest.W, Rest.H - Gap);
  TakeBottom (Rest, ToPxInt (26), &mView.Body, &mView.Footer);
  mView.Body.H -= Gap;
  mView.Content = mView.Body;
  mView.Scheme  = A->Scheme;
  mView.Laid    = TRUE;

  ShapesReset ();
  IconsReset ();
  TextFontsReset ();
  FrameInvalidate ();
  mFitCount    = 0;
  mLines.Valid = FALSE;
  // The dialog and the toast fade in again, and the bar starts over.
  mView.OverlayShown = FALSE;
  mView.ToastUntil   = (UINT64)-1;
  mView.BarShown     = FALSE;
}

// Opens a window covering the picture, for the screen, the dialog or the
// toast.
STATIC BOOLEAN
BeginWindow (IN CONST CHAR8 *Name)
{
  mu_Rect Full = mu_rect (0, 0, mView.W, mView.H);

  mu_get_container (mView.Ctx, Name)->rect = Full;
  return mu_begin_window_ex (mView.Ctx, Name, Full, WINDOW_OPTIONS) != 0;
}

// Makes the frame's command list from the app.
STATIC VOID
Build (IN OUT APP *A)
{
  INT32   W;
  INT32   H;
  BOOLEAN Footer  = HasFooter (A);
  SCREEN  Screen  = A->Screen == SCREEN_CONFIRM ? SCREEN_MENU : A->Screen;
  BOOLEAN WithBar = Screen == SCREEN_MENU && A->Countdown;

  mView.App       = A;
  mView.Now       = mView.Clock ();
  mView.LastFrame = mView.Now;
  mView.Animating = FALSE;
  mView.Alpha     = 255;
  FrameSize (&W, &H);
  if (W != mView.W || H != mView.H || A->Scheme != mView.Scheme ||
      !mView.Laid) {
    mView.W = W;
    mView.H = H;
    Layout (A);
  }
  mView.Body = mView.Content;
  if (!Footer) {
    mView.Body.H = Bottom (mView.Footer) - mView.Body.Y;
  }
  if (!WithBar) {
    mView.BarShown = FALSE;
  }
  if (Screen != SCREEN_BOOTING) {
    mView.Sweeping = FALSE;
  }

  mView.Ctx = FrameBegin ();
  if (BeginWindow ("screen")) {
    HeaderDraw (A);
    if (Footer) {
      FooterDraw (A);
    }
    switch (Screen) {
    case SCREEN_MENU:
    case SCREEN_CONFIRM:
      MenuDraw (A);
      break;
    case SCREEN_MODELS:
      ModelsDraw (A);
      break;
    case SCREEN_ENTRIES:
      EntriesDraw (A);
      break;
    case SCREEN_LINES:
      LinesDraw (A);
      break;
    case SCREEN_SCANNING:
      ScanningDraw ();
      break;
    case SCREEN_BOOTING:
      BootingDraw (A);
      break;
    case SCREEN_UNINSTALLING:
      UninstallingDraw ();
      break;
    case SCREEN_RESTARTING:
      RestartingDraw (A);
      break;
    case SCREEN_ERROR:
      ErrorDraw (A);
      break;
    }
    mu_end_window (mView.Ctx);
  }

  // The dialog and the toast stack in the order they appeared.
  if (A->Screen == SCREEN_CONFIRM) {
    if (!mView.OverlayShown || mView.OverlayKind != A->Confirm) {
      mView.OverlayShown = TRUE;
      mView.OverlayFaded = FALSE;
      mView.OverlayKind  = A->Confirm;
      mView.OverlayTick  = mView.Now;
      mu_bring_to_front (mView.Ctx, mu_get_container (mView.Ctx, "overlay"));
    }
    if (BeginWindow ("overlay")) {
      mView.Alpha =
        Fade (mView.OverlayTick, OVERLAY_FADE_MS, &mView.OverlayFaded);
      OverlayDraw (A);
      mView.Alpha = 255;
      mu_end_window (mView.Ctx);
    }
  } else {
    mView.OverlayShown = FALSE;
  }
  if (A->ToastUntil != mView.ToastUntil) {
    mView.ToastUntil = A->ToastUntil;
    mView.ToastFaded = FALSE;
    mView.ToastTick  = mView.Now;
    if (A->ToastUntil) {
      mu_bring_to_front (mView.Ctx, mu_get_container (mView.Ctx, "toast"));
    }
  }
  if (A->ToastUntil && BeginWindow ("toast")) {
    mView.Alpha = Fade (mView.ToastTick, TOAST_FADE_MS, &mView.ToastFaded);
    ToastDraw (A);
    mView.Alpha = 255;
    mu_end_window (mView.Ctx);
  }
  FrameEnd ();
}

BOOLEAN
ViewInit (
  IN CONST PANEL *Panel,
  IN CONST UINT8 *Font,
  IN UINTN        FontLength
)
{
  BOOLEAN Ok = FontLoad (Font, FontLength);

  // CJK glyphs, after the font's own.
  Ok = FontAddFallback (gOablAssetFontCjk, gOablAssetFontCjkSize) && Ok;

  ZeroMem (&mView, sizeof (mView));
  mView.Clock = Panel->Now;
  if (!FrameInit (Panel)) {
    return FALSE;
  }
  mView.Ready = TRUE;
  return Ok;
}

VOID
ViewRotate (IN UINT16 Degrees)
{
  FrameRotate (Degrees);
}

VOID
ViewLogicalSize (
  OUT UINT32 *W,
  OUT UINT32 *H
)
{
  INT32 LogicalW;
  INT32 LogicalH;

  FrameSize (&LogicalW, &LogicalH);
  *W = (UINT32)LogicalW;
  *H = (UINT32)LogicalH;
}

VOID
ViewSync (IN OUT APP *A)
{
  if (!mView.Ready) {
    return;
  }
  Build (A);
  A->Dirty = FALSE;
}

VOID
ViewRun (VOID)
{
  if (!mView.Ready) {
    return;
  }
  if (mView.Animating && mView.App &&
      mView.Clock () - mView.LastFrame >= FRAME_MS) {
    Build (mView.App);
  }
  FrameDraw (FALSE);
}

VOID
ViewSettle (VOID)
{
  if (!mView.Ready) {
    return;
  }
  // Finish the fades so still pictures show where they end.
  mView.OverlayFaded = TRUE;
  mView.ToastFaded   = TRUE;
  if (mView.App) {
    Build (mView.App);
  }
  FrameDraw (FALSE);
}

VOID
ViewDone (VOID)
{
  FrameDone ();
  ZeroMem (&mView, sizeof (mView));
}

VOID
ViewRedraw (IN BOOLEAN Everything)
{
  if (mView.Ready) {
    FrameDraw (Everything);
  }
}
