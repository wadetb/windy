// clang-format off
#include <stddef.h>
#include <WindowsX.h>
#include <ShellScalingAPI.h>
#include <GDIPlus.h>
// clang-format on

// Restricts the overlay to half the monitor, so as not to obscure the debugger.
#define HALF_MONITOR 1

#define HOTKEY_ID 1
#define HOTKEY_META MOD_WIN
#define HOTKEY_CODE VK_OEM_3
//#define HOTKEY_META 0
//#define HOTKEY_CODE VK_CAPITAL

#define IDR_ICON 1

#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "User32.lib")

struct {
  HINSTANCE hInst;
} win;

struct Point {
  int x;
  int y;
};

struct Bounds {
  int x;
  int y;
  int width;
  int height;
};

struct {
  HWND hWnd;
  struct Bounds placement;
} onDeck;

struct {
  struct Monitor *monitor;
  HWND hWnd;
  struct Bounds bounds;
  bool isOpen;
} overlay;

void FatalWin32Error(const char *format, ...) { __debugbreak(); }

#define CheckWin32(x_)                                                                                                 \
  do {                                                                                                                 \
    if (!(x_)) {                                                                                                       \
      FatalWin32Error("" #x_ " failed");                                                                               \
    }                                                                                                                  \
  } while (0)

void ReportError(const char *format, ...) { __debugbreak(); }

void FatalError(const char *format, ...) { __debugbreak(); }

void *AllocateBytes(size_t size, size_t count, const char *name) {
  void *mem = calloc(count, size);
  if (mem == NULL)
    FatalError("Allocation of %d %s%s failed", count, name, count > 1 ? "s" : "");
  return mem;
}

#define Allocate(type_) (type_ *)AllocateBytes(sizeof(type_), 1, #type_)
#define AllocateArray(type_, count_) (type_ *)AllocateBytes(sizeof(type_), count_, #type_)

#define OffsetOf(type_, member_)                                                                                       \
  (size_t)((ptrdiff_t) & reinterpret_cast<const volatile char &>((((type_ *)0)->member_)))
#define Unwrap(type_, member_, ptr_) (type_ *)((char *)ptr_ - OffsetOf(type_, member_))
#define Wrap(ptr_, member_) (&((ptr_)->member_))

#define AssertMessage(condition_, message_)                                                                            \
  do {                                                                                                                 \
    if (!(condition_))                                                                                                 \
      FatalError message_;                                                                                             \
  } while (0)
#define AssertNull(pointer_) AssertMessage((pointer_) == NULL, ("%s is not NULL", #pointer_))
#define AssertNotNull(pointer_) AssertMessage((pointer_) != NULL, ("%s is NULL", #pointer_))
#define AssertGreater(a_, b_) AssertMessage((a_) > (b_), ("%s (%d) is not greater than %s (%d)", #a_, (a_), #b_, (b_)))
#define AssertIndex(index_, count_)                                                                                    \
  AssertMessage((index_) >= 0 && (index_) < (count_),                                                                  \
                ("%s (%d) does not index %s (%d)", #index_, (index_), #count_, (count_)))

struct {
  PAINTSTRUCT ps;
  HDC hdc;
  Gdiplus::Graphics *g;
} draw;

void DrawText(int x, int y, int size, const char *text) {
  Gdiplus::SolidBrush brush(Gdiplus::Color(255, 0, 0, 0));
  Gdiplus::FontFamily fontFamily(L"Times New Roman");
  Gdiplus::Font font(&fontFamily, (float)size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::PointF pointF((float)x - overlay.bounds.x, (float)y - overlay.bounds.y);

  size_t chars;
  WCHAR wideText[_MAX_PATH];
  mbstowcs_s(&chars, wideText, _MAX_PATH, text, strlen(text));

  draw.g->DrawString(wideText, -1, &font, pointF, &brush);
}

#define OVERLAY_ALPHA 200

enum Dimension {
  Dimension_BorderInset = 0,
};

enum LineStyle {
  LineStyle_Border,
  LineStyle_Focus,
  LineStyle_Action,
  LineStyle_ActionHint,
};

void MakeLineStyle(Gdiplus::Pen *pen, LineStyle style) {
  switch (style) {
  case LineStyle_Border:
    pen->SetColor(Gdiplus::Color(255, 0x1f, 0x1f, 0xff));
    pen->SetWidth(2.5f);
    pen->SetDashStyle(Gdiplus::DashStyleSolid);
    break;
  case LineStyle_Focus:
    pen->SetColor(Gdiplus::Color(255, 128, 128, 128));
    pen->SetWidth(3);
    pen->SetDashStyle(Gdiplus::DashStyleDashDot);
    break;
  case LineStyle_Action:
    pen->SetColor(Gdiplus::Color(255, 0xf3, 0xa1, 0x16));
    pen->SetWidth(3.5f);
    pen->SetDashStyle(Gdiplus::DashStyleDashDot);
    break;
  case LineStyle_ActionHint:
    pen->SetColor(Gdiplus::Color(60, 0x40, 0x40, 0x40));
    pen->SetWidth(3.5f);
    pen->SetDashStyle(Gdiplus::DashStyleDashDot);
    break;
  default:
    break;
  }
  pen->SetAlignment(Gdiplus::PenAlignmentInset);
}

void DrawLine(struct Point from, struct Point to, LineStyle style) {
  Gdiplus::Pen pen(Gdiplus::Color(255, 0, 0, 0), 1);
  MakeLineStyle(&pen, style);

  draw.g->DrawLine(&pen, from.x - overlay.bounds.x, from.y - overlay.bounds.y, to.x - overlay.bounds.x,
                   to.y - overlay.bounds.y);
}

void DrawRoundedRectangle(struct Bounds bounds, int diameter, LineStyle style) {
  if (diameter > bounds.width)
    diameter = bounds.width;
  if (diameter > bounds.height)
    diameter = bounds.height;

  Gdiplus::Rect corner(bounds.x - overlay.bounds.x, bounds.y - overlay.bounds.y, diameter, diameter);
  Gdiplus::GraphicsPath path;
  path.AddArc(corner, 180, 90);
  corner.X += bounds.width - diameter - 1;
  path.AddArc(corner, 270, 90);
  corner.Y += bounds.height - diameter - 1;
  path.AddArc(corner, 0, 90);
  corner.X -= bounds.width - diameter - 1;
  path.AddArc(corner, 90, 90);
  path.CloseFigure();

  Gdiplus::Pen pen(Gdiplus::Color(255, 0, 0, 0), 1);
  MakeLineStyle(&pen, style);

  draw.g->DrawPath(&pen, &path);
}

void DrawRectangle(struct Bounds bounds, LineStyle style) {
  Gdiplus::Pen pen(Gdiplus::Color(255, 0, 0, 0), 1);
  MakeLineStyle(&pen, style);

  draw.g->DrawRectangle(&pen, bounds.x - overlay.bounds.x, bounds.y - overlay.bounds.y, bounds.width, bounds.height);
}

struct Point MakePoint(int x, int y) {
  struct Point point;
  point.x = x;
  point.y = y;
  return point;
}

bool PointInBounds(struct Point point, struct Bounds bounds) {
  if (point.x < bounds.x)
    return false;
  if (point.x >= bounds.x + bounds.width)
    return false;
  if (point.y < bounds.y)
    return false;
  if (point.y >= bounds.y + bounds.height)
    return false;
  return true;
}

struct Point BoundsMidpoint(struct Bounds bounds) {
  struct Point midPoint;
  midPoint.x = bounds.x + bounds.width / 2;
  midPoint.y = bounds.y + bounds.height / 2;
  return midPoint;
}

struct Input {
  bool used;
  unsigned int sequence;
  struct Point position;
  int buttons;
  int key;
  bool shift;
};

struct Input oldInput;
struct Input newInput;

struct Bin {
  void (*onDrawFn)(struct Bin *bin);
  void (*onInputFn)(struct Bin *bin);
  void (*onLayoutFn)(struct Bin *bin);
  void (*onDestroyFn)(struct Bin *bin);

  struct Bounds bounds;
};

struct Shelf {
  struct Bin bin;

  enum ShelfDirection direction;

  int slotCount;

  int hoverSlot;

  struct Bin **bins;
};

enum ShelfDirection { ShelfDirection_Horizontal, ShelfDirection_Vertical };

struct Shelf *NewShelf(enum ShelfDirection direction, int count);
struct Bounds ShelfMakeCellBounds(struct Shelf *shelf, int slot);

enum CellAction { CellAction_None, CellAction_SplitHorizontal, CellAction_SplitVertical };

struct Cell {
  struct Bin bin;

  unsigned int sequence;
  enum CellAction previewAction;
  struct Bin *subBin;
  HWND hWnd;
};

void PlaceOnDeckWindow();

void CellInput(struct Bin *bin) {
  AssertNotNull(bin);

  struct Cell *cell = Unwrap(struct Cell, bin, bin);

  cell->sequence = newInput.sequence;

  if (cell->subBin != NULL) {
    if (cell->subBin->onInputFn)
      cell->subBin->onInputFn(cell->subBin);
  } else {
    struct Point midPoint = BoundsMidpoint(cell->bin.bounds);
    int xDelta = abs(newInput.position.x - midPoint.x);
    int yDelta = abs(newInput.position.y - midPoint.y);

    cell->previewAction = CellAction_None;
    if (xDelta < yDelta) {
      if (xDelta < 10)
        cell->previewAction = CellAction_SplitHorizontal;
    } else {
      if (yDelta < 10)
        cell->previewAction = CellAction_SplitVertical;
    }

    if (cell->previewAction == CellAction_SplitHorizontal) {
      if ((newInput.buttons & MK_LBUTTON) && !(oldInput.buttons & MK_LBUTTON)) {
        struct Shelf *newShelf = NewShelf(ShelfDirection_Horizontal, 2);
        struct Bin *newBin = Wrap(newShelf, bin);
        cell->subBin = newBin;
        cell->subBin->bounds = cell->bin.bounds;
        if (cell->subBin->onLayoutFn)
          cell->subBin->onLayoutFn(cell->subBin);
        cell->previewAction = CellAction_None;
      }
    } else if (cell->previewAction == CellAction_SplitVertical) {
      if ((newInput.buttons & MK_LBUTTON) && !(oldInput.buttons & MK_LBUTTON)) {
        struct Shelf *newShelf = NewShelf(ShelfDirection_Vertical, 2);
        struct Bin *newBin = Wrap(newShelf, bin);
        cell->subBin = newBin;
        cell->subBin->bounds = cell->bin.bounds;
        if (cell->subBin->onLayoutFn)
          cell->subBin->onLayoutFn(cell->subBin);
        cell->previewAction = CellAction_None;
      }
      // cell->hWnd = onDeck.hWnd;
      // onDeck.placement = cell->bin.bounds;
      // PlaceOnDeckWindow();
    }
  }
}

void CellDraw(struct Bin *bin) {
  AssertNotNull(bin);

  struct Cell *cell = Unwrap(struct Cell, bin, bin);

  DrawRoundedRectangle(cell->bin.bounds, 5, LineStyle_Border);

  if (cell->subBin != NULL) {
    if (cell->subBin->onDrawFn)
      cell->subBin->onDrawFn(cell->subBin);
  } else {
    if (cell->sequence == newInput.sequence) {
      struct Point midPoint = BoundsMidpoint(cell->bin.bounds);

      struct Point from, to;
      from = MakePoint(cell->bin.bounds.x, midPoint.y);
      to = MakePoint(cell->bin.bounds.x + cell->bin.bounds.width, midPoint.y);
      DrawLine(from, to, cell->previewAction == CellAction_SplitVertical ? LineStyle_Action : LineStyle_ActionHint);

      from = MakePoint(midPoint.x, cell->bin.bounds.y);
      to = MakePoint(midPoint.x, cell->bin.bounds.y + cell->bin.bounds.height);
      DrawLine(from, to, cell->previewAction == CellAction_SplitHorizontal ? LineStyle_Action : LineStyle_ActionHint);
    }
  }
}

void CellLayout(struct Bin *bin) {
  AssertNotNull(bin);

  struct Cell *cell = Unwrap(struct Cell, bin, bin);

  if (cell->subBin != NULL) {
    cell->subBin->bounds = cell->bin.bounds;
    if (cell->subBin->onLayoutFn)
      cell->subBin->onLayoutFn(cell->subBin);
  }
}

void CellDestroy(struct Bin *bin) {
  AssertNotNull(bin);

  struct Cell *cell = Unwrap(struct Cell, bin, bin);

  if (cell->subBin != NULL) {
    if (cell->subBin->onDestroyFn)
      cell->subBin->onDestroyFn(cell->subBin);
  }
}

struct Cell *NewCell() {
  struct Cell *cell = Allocate(struct Cell);
  cell->bin.onInputFn = CellInput;
  cell->bin.onDrawFn = CellDraw;
  cell->bin.onLayoutFn = CellLayout;
  cell->bin.onDestroyFn = CellDestroy;
  return cell;
}

struct Bin *ShelfGet(struct Shelf *shelf, int slot) {
  AssertNotNull(shelf);
  AssertNotNull(shelf->bins);
  AssertIndex(slot, shelf->slotCount);

  return shelf->bins[slot];
}

void ShelfPut(struct Shelf *shelf, int slot, struct Bin *bin) {
  AssertNotNull(shelf);
  AssertNotNull(shelf->bins);
  AssertIndex(slot, shelf->slotCount);

  AssertNull(shelf->bins[slot]);

  shelf->bins[slot] = bin;

  if (bin != NULL) {
    bin->bounds = ShelfMakeCellBounds(shelf, slot);
    if (bin->onLayoutFn)
      bin->onLayoutFn(bin);
  }
}

void ShelfClear(struct Shelf *shelf, int slot) {
  AssertNotNull(shelf);
  AssertNotNull(shelf->bins);
  AssertIndex(slot, shelf->slotCount);

  struct Bin *bin = shelf->bins[slot];
  if (bin != NULL) {
    if (bin->onDestroyFn != NULL)
      bin->onDestroyFn(bin);

    free(bin);
    shelf->bins[slot] = NULL;
  }
}

void ShelfInsert(struct Shelf *shelf, int newSlot) {
  AssertNotNull(shelf);
  AssertIndex(newSlot, shelf->slotCount + 1);

  struct Bin **oldBins = shelf->bins;
  AssertNotNull(oldBins);

  shelf->slotCount += 1;
  shelf->bins = AllocateArray(struct Bin *, shelf->slotCount);

  for (int slot = 0; slot < newSlot; slot++)
    ShelfPut(shelf, slot, oldBins[slot]);

  struct Cell *newCell = NewCell();
  struct Bin *newBin = Wrap(newCell, bin);
  ShelfPut(shelf, newSlot, newBin);

  for (int slot = newSlot + 1; slot < shelf->slotCount; slot++)
    ShelfPut(shelf, slot, oldBins[slot - 1]);

  free(oldBins);
}

void ShelfDelete(struct Shelf *shelf, int oldSlot) {
  AssertNotNull(shelf);
  AssertIndex(oldSlot, shelf->slotCount);

  ShelfClear(shelf, oldSlot);

  struct Bin **oldBins = shelf->bins;
  AssertNotNull(oldBins);

  shelf->slotCount -= 1;
  shelf->bins = AllocateArray(struct Bin *, shelf->slotCount);

  for (int slot = 0; slot < oldSlot; slot++)
    ShelfPut(shelf, slot, oldBins[slot]);

  for (int slot = oldSlot; slot < shelf->slotCount; slot++)
    ShelfPut(shelf, slot, oldBins[slot + 1]);

  free(oldBins);
}

struct Bounds ShelfMakeCellBounds(struct Shelf *shelf, int slot) {
  AssertNotNull(shelf);
  AssertIndex(slot, shelf->slotCount);

  AssertGreater(shelf->slotCount, 0);

  struct Bounds bounds;

  if (shelf->direction == ShelfDirection_Vertical) {
    bounds.width = shelf->bin.bounds.width - 2 * Dimension_BorderInset;
    bounds.height = (shelf->bin.bounds.height - (shelf->slotCount + 1) * Dimension_BorderInset) / shelf->slotCount;
    bounds.x = shelf->bin.bounds.x + Dimension_BorderInset;
    bounds.y = shelf->bin.bounds.y + slot * (bounds.height + Dimension_BorderInset) + Dimension_BorderInset;
  } else {
    bounds.width = (shelf->bin.bounds.width - (shelf->slotCount + 1) * Dimension_BorderInset) / shelf->slotCount;
    bounds.height = shelf->bin.bounds.height - 2 * Dimension_BorderInset;
    bounds.x = shelf->bin.bounds.x + slot * (bounds.width + Dimension_BorderInset) + Dimension_BorderInset;
    bounds.y = shelf->bin.bounds.y + Dimension_BorderInset;
  }

  return bounds;
}

void ShelfDraw(struct Bin *bin) {
  AssertNotNull(bin);

  struct Shelf *shelf = Unwrap(struct Shelf, bin, bin);

  for (int slot = 0; slot < shelf->slotCount; slot++) {
    struct Bin *bin = ShelfGet(shelf, slot);
    if (bin->onDrawFn)
      bin->onDrawFn(bin);
  }
}

void ShelfInput(struct Bin *bin) {
  AssertNotNull(bin);

  struct Shelf *shelf = Unwrap(struct Shelf, bin, bin);

  shelf->hoverSlot = -1;

  for (int slot = 0; slot < shelf->slotCount; slot++) {
    struct Bounds bounds = ShelfMakeCellBounds(shelf, slot);
    if (PointInBounds(newInput.position, bounds)) {
      shelf->hoverSlot = slot;
    }
  }

  if (shelf->hoverSlot != -1) {
    struct Bin *hoverBin = ShelfGet(shelf, shelf->hoverSlot);
    if (hoverBin != NULL) {
      if (hoverBin->onInputFn)
        hoverBin->onInputFn(hoverBin);
    }

    if (!newInput.used) {
      if (newInput.key == 'X') {
        if (shelf->slotCount > 1) {
          ShelfDelete(shelf, shelf->hoverSlot);
          newInput.used = true;
        }
        // Pass other cases up to the parent to deal with.
      }

      if (newInput.key == 'H') {
        if (shelf->direction == ShelfDirection_Horizontal) {
          ShelfInsert(shelf, shelf->hoverSlot);
          newInput.used = true;
        } else {
          ShelfClear(shelf, shelf->hoverSlot);
          struct Shelf *newShelf = NewShelf(ShelfDirection_Horizontal, 2);
          struct Bin *newBin = Wrap(newShelf, bin);
          ShelfPut(shelf, shelf->hoverSlot, newBin);
          newInput.used = true;
        }
      }

      if (newInput.key == 'V') {
        if (shelf->direction == ShelfDirection_Vertical) {
          ShelfInsert(shelf, shelf->hoverSlot);
          newInput.used = true;
        } else {
          ShelfClear(shelf, shelf->hoverSlot);
          struct Shelf *newShelf = NewShelf(ShelfDirection_Vertical, 2);
          struct Bin *newBin = Wrap(newShelf, bin);
          ShelfPut(shelf, shelf->hoverSlot, newBin);
          newInput.used = true;
        }
      }
    }
  }
}

void ShelfLayout(struct Bin *bin) {
  AssertNotNull(bin);

  struct Shelf *shelf = Unwrap(struct Shelf, bin, bin);

  for (int slot = 0; slot < shelf->slotCount; slot++) {
    struct Bin *bin = ShelfGet(shelf, slot);
    if (bin != NULL) {
      bin->bounds = ShelfMakeCellBounds(shelf, slot);
      if (bin->onLayoutFn != NULL)
        bin->onLayoutFn(bin);
    }
  }
}

void ShelfDestroy(struct Bin *bin) {
  AssertNotNull(bin);

  struct Shelf *shelf = Unwrap(struct Shelf, bin, bin);

  for (int slot = 0; slot < shelf->slotCount; slot++)
    ShelfClear(shelf, slot);

  free(shelf->bins);
}

struct Shelf *NewShelf(enum ShelfDirection direction, int count) {
  struct Shelf *shelf = Allocate(struct Shelf);
  shelf->bin.onDrawFn = ShelfDraw;
  shelf->bin.onInputFn = ShelfInput;
  shelf->bin.onLayoutFn = ShelfLayout;
  shelf->bin.onDestroyFn = ShelfDestroy;

  shelf->direction = direction;

  shelf->slotCount = count;
  shelf->bins = AllocateArray(struct Bin *, shelf->slotCount);

  for (int slot = 0; slot < shelf->slotCount; slot++) {
    struct Cell *newCell = NewCell();
    struct Bin *newBin = Wrap(newCell, bin);
    ShelfPut(shelf, slot, newBin);
  }

  return shelf;
}

struct Grid {
  struct Bin bin;

  int rowCount;
  int columnCount;

  int hoverRow;
  int hoverColumn;

  struct Bin **bins;
};

struct Grid *NewGrid();
struct Bounds GridMakeCellBounds(struct Grid *grid, int row, int column);

struct Bin *Grid(struct Grid *grid, int row, int column) {
  AssertNotNull(grid);
  AssertNotNull(grid->bins);
  AssertIndex(row, grid->rowCount);
  AssertIndex(column, grid->columnCount);

  int index = grid->columnCount * row + column;

  return grid->bins[index];
}

void GridPut(struct Grid *grid, int row, int column, struct Bin *bin) {
  AssertNotNull(grid);
  AssertNotNull(grid->bins);
  AssertIndex(row, grid->rowCount);
  AssertIndex(column, grid->columnCount);

  int index = grid->columnCount * row + column;
  AssertNull(grid->bins[index]);

  grid->bins[index] = bin;

  if (bin != NULL) {
    bin->bounds = GridMakeCellBounds(grid, row, column);
    if (bin->onLayoutFn)
      bin->onLayoutFn(bin);
  }
}

void GridClear(struct Grid *grid, int row, int column) {
  AssertNotNull(grid);
  AssertNotNull(grid->bins);
  AssertIndex(row, grid->rowCount);
  AssertIndex(column, grid->columnCount);

  int index = grid->columnCount * row + column;

  struct Bin *bin = grid->bins[index];
  if (bin != NULL) {
    if (bin->onDestroyFn != NULL)
      bin->onDestroyFn(bin);

    free(bin);
    grid->bins[index] = NULL;
  }
}

void GridInsertRow(struct Grid *grid, int newRow) {
  AssertNotNull(grid);
  AssertIndex(newRow, grid->rowCount + 1);

  struct Bin **oldBins = grid->bins;
  AssertNotNull(oldBins);

  grid->rowCount += 1;
  grid->bins = AllocateArray(struct Bin *, grid->rowCount * grid->columnCount);

  for (int column = 0; column < grid->columnCount; column++) {
    for (int row = 0; row < newRow; row++)
      GridPut(grid, row, column, oldBins[grid->columnCount * row + column]);

    struct Cell *newCell = NewCell();
    struct Bin *newBin = Wrap(newCell, bin);
    GridPut(grid, newRow, column, newBin);

    for (int row = newRow + 1; row < grid->rowCount; row++)
      GridPut(grid, row, column, oldBins[grid->columnCount * (row - 1) + column]);
  }

  free(oldBins);
}

void GridDeleteRow(struct Grid *grid, int oldRow) {
  AssertNotNull(grid);
  AssertIndex(oldRow, grid->rowCount);

  for (int column = 0; column < grid->columnCount; column++)
    GridClear(grid, oldRow, column);

  struct Bin **oldBins = grid->bins;
  AssertNotNull(oldBins);

  grid->rowCount -= 1;
  grid->bins = AllocateArray(struct Bin *, grid->rowCount * grid->columnCount);

  for (int column = 0; column < grid->columnCount; column++) {
    for (int row = 0; row < oldRow; row++)
      GridPut(grid, row, column, oldBins[grid->columnCount * row + column]);

    for (int row = oldRow; row < grid->rowCount; row++)
      GridPut(grid, row, column, oldBins[grid->columnCount * (row + 1) + column]);
  }

  free(oldBins);
}

void GridInsertColumn(struct Grid *grid, int newColumn) {
  AssertNotNull(grid);
  AssertIndex(newColumn, grid->columnCount + 1);

  int oldColumnCount = grid->columnCount;
  struct Bin **oldBins = grid->bins;
  AssertNotNull(oldBins);

  grid->columnCount += 1;
  grid->bins = AllocateArray(struct Bin *, grid->rowCount * grid->columnCount);

  for (int row = 0; row < grid->rowCount; row++) {
    for (int column = 0; column < newColumn; column++)
      GridPut(grid, row, column, oldBins[oldColumnCount * row + column]);

    struct Cell *newCell = NewCell();
    struct Bin *newBin = Wrap(newCell, bin);
    GridPut(grid, row, newColumn, newBin);

    for (int column = newColumn + 1; column < grid->columnCount; column++)
      GridPut(grid, row, column, oldBins[oldColumnCount * row + column - 1]);
  }

  free(oldBins);
}

void GridDeleteColumn(struct Grid *grid, int oldColumn) {
  AssertNotNull(grid);
  AssertIndex(oldColumn, grid->columnCount);

  for (int row = 0; row < grid->rowCount; row++)
    GridClear(grid, row, oldColumn);

  int oldColumnCount = grid->columnCount;
  struct Bin **oldBins = grid->bins;
  AssertNotNull(oldBins);

  grid->columnCount -= 1;
  grid->bins = AllocateArray(struct Bin *, grid->rowCount * grid->columnCount);

  for (int row = 0; row < grid->rowCount; row++) {
    for (int column = 0; column < oldColumn; column++)
      GridPut(grid, row, column, oldBins[oldColumnCount * row + column]);

    for (int column = oldColumn; column < grid->columnCount; column++)
      GridPut(grid, row, column, oldBins[oldColumnCount * row + column + 1]);
  }

  free(oldBins);
}

struct Bounds GridMakeCellBounds(struct Grid *grid, int row, int column) {
  AssertNotNull(grid);
  AssertIndex(row, grid->rowCount);
  AssertIndex(column, grid->columnCount);

  AssertGreater(grid->columnCount, 0);
  AssertGreater(grid->rowCount, 0);

  struct Bounds bounds;
  bounds.width = (grid->bin.bounds.width - (grid->columnCount + 1) * Dimension_BorderInset) / grid->columnCount;
  bounds.height = (grid->bin.bounds.height - (grid->rowCount + 1) * Dimension_BorderInset) / grid->rowCount;
  bounds.x = grid->bin.bounds.x + column * (bounds.width + Dimension_BorderInset) + Dimension_BorderInset;
  bounds.y = grid->bin.bounds.y + row * (bounds.height + Dimension_BorderInset) + Dimension_BorderInset;
  return bounds;
}

void GridDraw(struct Bin *bin) {
  AssertNotNull(bin);

  struct Grid *grid = Unwrap(struct Grid, bin, bin);

  for (int row = 0; row < grid->rowCount; row++) {
    for (int column = 0; column < grid->columnCount; column++) {
      struct Bin *bin = Grid(grid, row, column);
      if (bin->onDrawFn)
        bin->onDrawFn(bin);
    }
  }
}

void GridInput(struct Bin *bin) {
  AssertNotNull(bin);

  struct Grid *grid = Unwrap(struct Grid, bin, bin);

  grid->hoverRow = -1;
  grid->hoverColumn = -1;

  for (int row = 0; row < grid->rowCount; row++) {
    for (int column = 0; column < grid->columnCount; column++) {
      struct Bounds bounds = GridMakeCellBounds(grid, row, column);
      if (PointInBounds(newInput.position, bounds)) {
        grid->hoverRow = row;
        grid->hoverColumn = column;
      }
    }
  }

  if (grid->hoverRow != -1 && grid->hoverColumn != -1) {
    struct Bin *hoverBin = Grid(grid, grid->hoverRow, grid->hoverColumn);
    if (hoverBin != NULL) {
      if (hoverBin->onInputFn)
        hoverBin->onInputFn(hoverBin);
    }

    if (!newInput.used) {
      if (newInput.key == 'X') {
        if (grid->rowCount > 1 && grid->columnCount == 1) {
          GridDeleteRow(grid, grid->hoverRow);
          newInput.used = true;
        } else if (grid->rowCount == 1 && grid->columnCount > 1) {
          GridDeleteColumn(grid, grid->hoverColumn);
          newInput.used = true;
        }
        // Pass other cases up to the parent to deal with.
      }

      if (newInput.key == 'H') {
        GridClear(grid, grid->hoverRow, grid->hoverColumn);
        struct Grid *newGrid = NewGrid();
        struct Bin *newBin = Wrap(newGrid, bin);
        GridPut(grid, grid->hoverRow, grid->hoverColumn, newBin);
        newInput.used = true;
      }

      if (newInput.key == 'C' && !newInput.shift) {
        GridInsertColumn(grid, grid->hoverColumn);
        newInput.used = true;
      }
      if (newInput.key == 'C' && newInput.shift) {
        if (grid->columnCount > 1)
          GridDeleteColumn(grid, grid->hoverColumn);
        newInput.used = true;
      }

      if (newInput.key == 'R' && !newInput.shift) {
        GridInsertRow(grid, grid->hoverRow);
        newInput.used = true;
      }

      if (newInput.key == 'R' && newInput.shift) {
        if (grid->rowCount > 1)
          GridDeleteRow(grid, grid->hoverRow);
        newInput.used = true;
      }
    }
  }
}

void GridLayout(struct Bin *bin) {
  AssertNotNull(bin);

  struct Grid *grid = Unwrap(struct Grid, bin, bin);

  for (int row = 0; row < grid->rowCount; row++) {
    for (int column = 0; column < grid->columnCount; column++) {
      struct Bin *bin = Grid(grid, row, column);
      if (bin != NULL) {
        bin->bounds = GridMakeCellBounds(grid, row, column);
        if (bin->onLayoutFn != NULL)
          bin->onLayoutFn(bin);
      }
    }
  }
}

void GridDestroy(struct Bin *bin) {
  AssertNotNull(bin);

  struct Grid *grid = Unwrap(struct Grid, bin, bin);

  for (int row = 0; row < grid->rowCount; row++)
    for (int column = 0; column < grid->columnCount; column++)
      GridClear(grid, row, column);

  free(grid->bins);
}

struct Grid *NewGrid() {
  struct Grid *grid = Allocate(struct Grid);
  grid->bin.onDrawFn = GridDraw;
  grid->bin.onInputFn = GridInput;
  grid->bin.onLayoutFn = GridLayout;
  grid->bin.onDestroyFn = GridDestroy;

  grid->rowCount = 1;
  grid->columnCount = 1;
  grid->bins = AllocateArray(struct Bin *, 1);

  struct Cell *newCell = NewCell();
  struct Bin *newBin = Wrap(newCell, bin);
  GridPut(grid, 0, 0, newBin);

  return grid;
}

#define MONITOR_LIMIT 16

struct Monitor {
  HMONITOR hMonitor;
  MONITORINFO info;
  struct Bounds bounds;
  struct Bin *root;
};

struct Monitor monitors[MAX_MONITORS];

void UpdateMonitorInfo(struct Monitor *monitor) {
  monitor->info = {sizeof(MONITORINFO)};
  CheckWin32(GetMonitorInfo(monitor->hMonitor, &monitor->info));

  monitor->bounds.x = monitor->info.rcWork.left;
  monitor->bounds.y = monitor->info.rcWork.top;
  monitor->bounds.width = monitor->info.rcWork.right - monitor->info.rcWork.left;
  monitor->bounds.height = monitor->info.rcWork.bottom - monitor->info.rcWork.top;
}

struct Monitor *GetMonitorAtCursor() {
  POINT mousePoint = {};
  CheckWin32(GetCursorPos(&mousePoint));

  HMONITOR hMonitor = MonitorFromPoint(mousePoint, MONITOR_DEFAULTTONULL);
  if (hMonitor == NULL) {
    ReportError("Mouse position %d %d was not over any monitor", mousePoint.x, mousePoint.y);
    return NULL;
  }

  for (int i = 0; i < MONITOR_LIMIT; i++) {
    struct Monitor *monitor = &monitors[i];
    if (monitor->hMonitor == NULL) {
      monitor->hMonitor = hMonitor;
      monitor->root = Wrap(NewShelf(ShelfDirection_Horizontal, 2), bin);
      UpdateMonitorInfo(monitor);
      return monitor;
    }
    if (monitor->hMonitor == hMonitor) {
      UpdateMonitorInfo(monitor);
      return monitor;
    }
  }

  return NULL;
}

void PickOnDeckWindow() {
  POINT mousePoint = {};
  CheckWin32(GetCursorPos(&mousePoint));

  onDeck.placement = {100, 100, 800, 600};

  HWND hWnd = WindowFromPoint(mousePoint);
  onDeck.hWnd = GetAncestor(hWnd, GA_ROOT);
}

void ClearOnDeckWindow() { onDeck.hWnd = NULL; }

void PlaceOnDeckWindow() {
  if (!onDeck.hWnd) {
    ReportError("Tried to place the on deck window when none was active");
    return;
  }

  SetWindowPos(onDeck.hWnd, NULL, onDeck.placement.x, onDeck.placement.y, onDeck.placement.width,
               onDeck.placement.height, SWP_SHOWWINDOW);
}

void ShowOverlay() {
  overlay.monitor = GetMonitorAtCursor();
  if (overlay.monitor == NULL)
    return;

  overlay.bounds = overlay.monitor->bounds;

#if HALF_MONITOR
  overlay.bounds.width = overlay.bounds.width / 2;
  overlay.bounds.x += overlay.bounds.width;
#endif

  SetWindowPos(overlay.hWnd, HWND_TOPMOST, overlay.bounds.x, overlay.bounds.y, overlay.bounds.width,
               overlay.bounds.height, SWP_SHOWWINDOW);

  struct Bin *rootBin = overlay.monitor->root;
  if (rootBin) {
    rootBin->bounds = overlay.bounds;
    if (rootBin->onLayoutFn)
      rootBin->onLayoutFn(rootBin);
  }

  overlay.isOpen = true;
}

void HideOverlay() {
  ShowWindow(overlay.hWnd, SW_HIDE);

  overlay.isOpen = false;
}

void OnOverlayHotkey() {
  if (!overlay.isOpen) {
    PickOnDeckWindow();
    ShowOverlay();
  } else {
    HideOverlay();
    ClearOnDeckWindow();
  }
}

void OnOverlayMouse(UINT message, UINT buttons, int x, int y) {
  if (!overlay.isOpen) {
    ReportError("Overlay received a mouse event %d at %d %d when it was not open", message, x, y);
    return;
  }

  oldInput = newInput;
  newInput.used = false;
  newInput.sequence++;
  newInput.position.x = x + overlay.bounds.x;
  newInput.position.y = y + overlay.bounds.y;
  newInput.buttons = buttons;
  newInput.key = 0;

  overlay.monitor->root->onInputFn(overlay.monitor->root);

  InvalidateRect(overlay.hWnd, NULL, TRUE);
}

void OnOverlayKey(UINT key) {
  if (!overlay.isOpen) {
    ReportError("Overlay received a key event %d when it was not open", key);
    return;
  }

  oldInput = newInput;
  newInput.used = false;
  newInput.key = key;
  newInput.shift = GetAsyncKeyState(VK_SHIFT) || GetAsyncKeyState(VK_LSHIFT);

  overlay.monitor->root->onInputFn(overlay.monitor->root);

  InvalidateRect(overlay.hWnd, NULL, TRUE);
}

void OnOverlayPaint() {
  if (!overlay.isOpen) {
    // ReportError("Overlay received a paint event");
    return;
  }

  draw.hdc = BeginPaint(overlay.hWnd, &draw.ps);

  RECT rc;
  GetClientRect(overlay.hWnd, &rc);

  HDC hdcMem = CreateCompatibleDC(draw.hdc);
  HBITMAP hbmMem = CreateCompatibleBitmap(draw.hdc, rc.right - rc.left, rc.bottom - rc.top);
  SelectObject(hdcMem, hbmMem);

  HBRUSH hbrBkGnd = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
  FillRect(hdcMem, &rc, hbrBkGnd);
  DeleteObject(hbrBkGnd);

  draw.g = new Gdiplus::Graphics(hdcMem);
  draw.g->SetSmoothingMode(Gdiplus::SmoothingMode::SmoothingModeAntiAlias);

  overlay.monitor->root->onDrawFn(overlay.monitor->root);

  delete draw.g;
  draw.g = NULL;

  BitBlt(draw.hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, hdcMem, 0, 0, SRCCOPY);

  DeleteObject(hbmMem);
  DeleteDC(hdcMem);

  EndPaint(overlay.hWnd, &draw.ps);
}

LRESULT CALLBACK OverlayWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_HOTKEY:
    switch (LOWORD(wParam)) {
    case HOTKEY_ID:
      OnOverlayHotkey();
      return 0;
    }
    break;

  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_MOUSEMOVE:
    OnOverlayMouse(message, (UINT)wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    break;

  case WM_KEYDOWN:
    OnOverlayKey((UINT)wParam);
    break;

  case WM_ERASEBKGND:
    return TRUE;

  case WM_PAINT:
    OnOverlayPaint();
    break;

  default:
    break;
  }

  return DefWindowProc(hWnd, message, wParam, lParam);
}

void CreateOverlay() {
  WNDCLASSEX wcex;
  ZeroMemory(&wcex, sizeof(wcex));
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = OverlayWindowProc;
  wcex.hInstance = win.hInst;
  wcex.hIcon = LoadIcon(win.hInst, MAKEINTRESOURCE(IDR_ICON));
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszClassName = "WINDY_MAIN";
  CheckWin32(RegisterClassEx(&wcex));

  RECT rc = {0, 0, 100, 100};
  CheckWin32(AdjustWindowRect(&rc, WS_POPUP, FALSE));

  overlay.hWnd = CreateWindow("WINDY_MAIN", "Windy", WS_POPUPWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                              rc.bottom - rc.top, NULL, NULL, win.hInst, NULL);

  SetWindowLong(overlay.hWnd, GWL_EXSTYLE, GetWindowLong(overlay.hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
  SetLayeredWindowAttributes(overlay.hWnd, 0, OVERLAY_ALPHA, LWA_ALPHA);

  CheckWin32(RegisterHotKey(overlay.hWnd, HOTKEY_ID, HOTKEY_META, HOTKEY_CODE));
}

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
  SetProcessDpiAwareness((PROCESS_DPI_AWARENESS)PROCESS_PER_MONITOR_DPI_AWARE);

  Gdiplus::GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR gdiplusToken;
  GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

  CreateOverlay();

  for (;;) {
    MSG msg;
    BOOL result = GetMessage(&msg, NULL, 0, 0);
    if (result < 0)
      FatalWin32Error("GetMessage failed");
    if (result == 0)
      break;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  Gdiplus::GdiplusShutdown(gdiplusToken);

  return 0;
}
