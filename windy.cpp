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

#define OVERLAY_ALPHA 200

#define BORDER_INSET 4
#define BORDER_WIDTH 8

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

void DrawRectangle(struct Bounds bounds, int lineWidth, bool dashed) {
  Gdiplus::Pen pen(Gdiplus::Color(255, 0, 0, 0), 10);
  pen.SetAlignment(Gdiplus::PenAlignmentInset);

  if (dashed)
    pen.SetDashStyle(Gdiplus::DashStyleDash);

  draw.g->DrawRectangle(&pen, bounds.x - overlay.bounds.x, bounds.y - overlay.bounds.y, bounds.width, bounds.height);
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

struct Input {
  bool used;
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

struct Cell {
  struct Bin bin;
  HWND hWnd;
};

void CellInput(struct Bin *bin) {
  AssertNotNull(bin);

  struct Cell *cell = Unwrap(struct Cell, bin, bin);

  onDeck.placement = cell->bin.bounds;
}

struct Cell *NewCell() {
  struct Cell *cell = Allocate(struct Cell);
  cell->bin.onInputFn = CellInput;
  return cell;
}

struct Shelf {
  struct Bin bin;

  int rowCount;
  int columnCount;

  int hoverRow;
  int hoverColumn;

  struct Bin **bins;
};

struct Shelf *NewShelf();
struct Bounds ShelfMakeCellBounds(struct Shelf *shelf, int row, int column);

struct Bin *ShelfGet(struct Shelf *shelf, int row, int column) {
  AssertNotNull(shelf);
  AssertNotNull(shelf->bins);
  AssertIndex(row, shelf->rowCount);
  AssertIndex(column, shelf->columnCount);

  int index = shelf->columnCount * row + column;

  return shelf->bins[index];
}

void ShelfPut(struct Shelf *shelf, int row, int column, struct Bin *bin) {
  AssertNotNull(shelf);
  AssertNotNull(shelf->bins);
  AssertIndex(row, shelf->rowCount);
  AssertIndex(column, shelf->columnCount);

  int index = shelf->columnCount * row + column;
  AssertNull(shelf->bins[index]);

  shelf->bins[index] = bin;

  if (bin != NULL) {
    bin->bounds = ShelfMakeCellBounds(shelf, row, column);
    if (bin->onLayoutFn)
      bin->onLayoutFn(bin);
  }
}

void ShelfClear(struct Shelf *shelf, int row, int column) {
  AssertNotNull(shelf);
  AssertNotNull(shelf->bins);
  AssertIndex(row, shelf->rowCount);
  AssertIndex(column, shelf->columnCount);

  int index = shelf->columnCount * row + column;

  struct Bin *bin = shelf->bins[index];
  if (bin != NULL) {
    if (bin->onDestroyFn != NULL)
      bin->onDestroyFn(bin);

    free(bin);
    shelf->bins[index] = NULL;
  }
}

void ShelfInsertRow(struct Shelf *shelf, int newRow) {
  AssertNotNull(shelf);
  AssertIndex(newRow, shelf->rowCount + 1);

  struct Bin **oldBins = shelf->bins;
  AssertNotNull(oldBins);

  shelf->rowCount += 1;
  shelf->bins = AllocateArray(struct Bin *, shelf->rowCount * shelf->columnCount);

  for (int column = 0; column < shelf->columnCount; column++) {
    for (int row = 0; row < newRow; row++)
      ShelfPut(shelf, row, column, oldBins[shelf->columnCount * row + column]);

    struct Cell *newCell = NewCell();
    struct Bin *newBin = Wrap(newCell, bin);
    ShelfPut(shelf, newRow, column, newBin);

    for (int row = newRow + 1; row < shelf->rowCount; row++)
      ShelfPut(shelf, row, column, oldBins[shelf->columnCount * (row - 1) + column]);
  }

  free(oldBins);
}

void ShelfDeleteRow(struct Shelf *shelf, int oldRow) {
  AssertNotNull(shelf);
  AssertIndex(oldRow, shelf->rowCount);

  for (int column = 0; column < shelf->columnCount; column++)
    ShelfClear(shelf, oldRow, column);

  struct Bin **oldBins = shelf->bins;
  AssertNotNull(oldBins);

  shelf->rowCount -= 1;
  shelf->bins = AllocateArray(struct Bin *, shelf->rowCount * shelf->columnCount);

  for (int column = 0; column < shelf->columnCount; column++) {
    for (int row = 0; row < oldRow; row++)
      ShelfPut(shelf, row, column, oldBins[shelf->columnCount * row + column]);

    for (int row = oldRow; row < shelf->rowCount; row++)
      ShelfPut(shelf, row, column, oldBins[shelf->columnCount * (row + 1) + column]);
  }

  free(oldBins);
}

void ShelfInsertColumn(struct Shelf *shelf, int newColumn) {
  AssertNotNull(shelf);
  AssertIndex(newColumn, shelf->columnCount + 1);

  int oldColumnCount = shelf->columnCount;
  struct Bin **oldBins = shelf->bins;
  AssertNotNull(oldBins);

  shelf->columnCount += 1;
  shelf->bins = AllocateArray(struct Bin *, shelf->rowCount * shelf->columnCount);

  for (int row = 0; row < shelf->rowCount; row++) {
    for (int column = 0; column < newColumn; column++)
      ShelfPut(shelf, row, column, oldBins[oldColumnCount * row + column]);

    struct Cell *newCell = NewCell();
    struct Bin *newBin = Wrap(newCell, bin);
    ShelfPut(shelf, row, newColumn, newBin);

    for (int column = newColumn + 1; column < shelf->columnCount; column++)
      ShelfPut(shelf, row, column, oldBins[oldColumnCount * row + column - 1]);
  }

  free(oldBins);
}

void ShelfDeleteColumn(struct Shelf *shelf, int oldColumn) {
  AssertNotNull(shelf);
  AssertIndex(oldColumn, shelf->columnCount);

  for (int row = 0; row < shelf->rowCount; row++)
    ShelfClear(shelf, row, oldColumn);

  int oldColumnCount = shelf->columnCount;
  struct Bin **oldBins = shelf->bins;
  AssertNotNull(oldBins);

  shelf->columnCount -= 1;
  shelf->bins = AllocateArray(struct Bin *, shelf->rowCount * shelf->columnCount);

  for (int row = 0; row < shelf->rowCount; row++) {
    for (int column = 0; column < oldColumn; column++)
      ShelfPut(shelf, row, column, oldBins[oldColumnCount * row + column]);

    for (int column = oldColumn; column < shelf->columnCount; column++)
      ShelfPut(shelf, row, column, oldBins[oldColumnCount * row + column + 1]);
  }

  free(oldBins);
}

struct Bounds ShelfMakeCellBounds(struct Shelf *shelf, int row, int column) {
  AssertNotNull(shelf);
  AssertIndex(row, shelf->rowCount);
  AssertIndex(column, shelf->columnCount);

  AssertGreater(shelf->columnCount, 0);
  AssertGreater(shelf->rowCount, 0);

  struct Bounds bounds;
  bounds.width = (shelf->bin.bounds.width - (shelf->columnCount + 1) * BORDER_INSET) / shelf->columnCount;
  bounds.height = (shelf->bin.bounds.height - (shelf->rowCount + 1) * BORDER_INSET) / shelf->rowCount;
  bounds.x = shelf->bin.bounds.x + column * (bounds.width + BORDER_INSET) + BORDER_INSET;
  bounds.y = shelf->bin.bounds.y + row * (bounds.height + BORDER_INSET) + BORDER_INSET;
  return bounds;
}

void ShelfDraw(struct Bin *bin) {
  AssertNotNull(bin);

  struct Shelf *shelf = Unwrap(struct Shelf, bin, bin);

  for (int row = 0; row < shelf->rowCount; row++) {
    for (int column = 0; column < shelf->columnCount; column++) {
      struct Bin *bin = ShelfGet(shelf, row, column);
      if (bin->onDrawFn)
        bin->onDrawFn(bin);

      struct Bounds bounds = ShelfMakeCellBounds(shelf, row, column);

      bool dashed = false;
      if (column == shelf->hoverColumn && row == shelf->hoverRow)
        dashed = true;

      DrawRectangle(bounds, BORDER_WIDTH, dashed);
    }
  }
}

void ShelfInput(struct Bin *bin) {
  AssertNotNull(bin);

  struct Shelf *shelf = Unwrap(struct Shelf, bin, bin);

  shelf->hoverRow = -1;
  shelf->hoverColumn = -1;

  for (int row = 0; row < shelf->rowCount; row++) {
    for (int column = 0; column < shelf->columnCount; column++) {
      struct Bounds bounds = ShelfMakeCellBounds(shelf, row, column);
      if (PointInBounds(newInput.position, bounds)) {
        shelf->hoverRow = row;
        shelf->hoverColumn = column;
      }
    }
  }

  if (shelf->hoverRow != -1 && shelf->hoverColumn != -1) {
    struct Bin *hoverBin = ShelfGet(shelf, shelf->hoverRow, shelf->hoverColumn);
    if (hoverBin != NULL) {
      if (hoverBin->onInputFn)
        hoverBin->onInputFn(hoverBin);
    }

    if (!newInput.used) {
      if (newInput.key == 'H') {
        ShelfClear(shelf, shelf->hoverRow, shelf->hoverColumn);
        struct Shelf *newShelf = NewShelf();
        struct Bin *newBin = Wrap(newShelf, bin);
        ShelfPut(shelf, shelf->hoverRow, shelf->hoverColumn, newBin);
        newInput.used = true;
      }

      if (newInput.key == 'C' && !newInput.shift) {
        ShelfInsertColumn(shelf, shelf->hoverColumn);
        newInput.used = true;
      }
      if (newInput.key == 'C' && newInput.shift) {
        if (shelf->columnCount > 1)
          ShelfDeleteColumn(shelf, shelf->hoverColumn);
        newInput.used = true;
      }

      if (newInput.key == 'R' && !newInput.shift) {
        ShelfInsertRow(shelf, shelf->hoverRow);
        newInput.used = true;
      }

      if (newInput.key == 'R' && newInput.shift) {
        if (shelf->rowCount > 1)
          ShelfDeleteRow(shelf, shelf->hoverRow);
        newInput.used = true;
      }
    }
  }
}

void ShelfLayout(struct Bin *bin) {
  AssertNotNull(bin);

  struct Shelf *shelf = Unwrap(struct Shelf, bin, bin);

  for (int row = 0; row < shelf->rowCount; row++) {
    for (int column = 0; column < shelf->columnCount; column++) {
      struct Bin *bin = ShelfGet(shelf, row, column);
      if (bin != NULL) {
        bin->bounds = ShelfMakeCellBounds(shelf, row, column);
        if (bin->onLayoutFn != NULL)
          bin->onLayoutFn(bin);
      }
    }
  }
}

void ShelfDestroy(struct Bin *bin) {
  AssertNotNull(bin);

  struct Shelf *shelf = Unwrap(struct Shelf, bin, bin);

  for (int row = 0; row < shelf->rowCount; row++)
    for (int column = 0; column < shelf->columnCount; column++)
      ShelfClear(shelf, row, column);

  free(shelf->bins);
}

struct Shelf *NewShelf() {
  struct Shelf *shelf = Allocate(struct Shelf);
  shelf->bin.onDrawFn = ShelfDraw;
  shelf->bin.onInputFn = ShelfInput;
  shelf->bin.onLayoutFn = ShelfLayout;
  shelf->bin.onDestroyFn = ShelfDestroy;

  shelf->rowCount = 1;
  shelf->columnCount = 1;
  shelf->bins = AllocateArray(struct Bin *, 1);

  struct Cell *newCell = NewCell();
  struct Bin *newBin = Wrap(newCell, bin);
  ShelfPut(shelf, 0, 0, newBin);

  return shelf;
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
      monitor->root = Wrap(NewShelf(), bin);
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
  newInput.position.x = x + overlay.bounds.x;
  newInput.position.y = y + overlay.bounds.y;
  newInput.buttons = buttons;
  newInput.key = 0;

  overlay.monitor->root->onInputFn(overlay.monitor->root);

  InvalidateRect(overlay.hWnd, NULL, TRUE);

  if (message == WM_LBUTTONUP) {
    HideOverlay();
    PlaceOnDeckWindow();
  }
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

  if (key == VK_ESCAPE) {
    HideOverlay();
    ClearOnDeckWindow();
  }
}

void OnOverlayPaint() {
  if (!overlay.isOpen) {
    ReportError("Overlay received a paint event");
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
      break;
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
