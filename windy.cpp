#include <GDIPlus.h>
#include <ShellScalingAPI.h>
#include <WindowsX.h>
#include <stddef.h>

// Restricts the overlay to half the monitor, so as not to obscure the debugger.
#define HALF_MONITOR 0

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

void FatalWin32Error(const char *format, ...) {}

#define CheckWin32(x_)                                                         \
  do {                                                                         \
    if (!(x_)) {                                                               \
      FatalWin32Error("" #x_ " failed");                                       \
    }                                                                          \
  } while (0)

void ReportError(const char *format, ...) {}

void FatalError(const char *format, ...) {}

void *Allocate(size_t size, size_t count, const char *name) {
  void *mem = calloc(count, size);
  if (mem == NULL)
    FatalError("Allocation of %d %s%s failed", count, name,
               count > 1 ? "s" : "");
  return mem;
}

#define AllocateStruct(type_) (type_ *)Allocate(sizeof(type_), 1, #type_)

struct {
  PAINTSTRUCT ps;
  HDC hdc;
  Gdiplus::Graphics *g;
} draw;

void DrawText(int x, int y, int size, const char *text) {
  Gdiplus::SolidBrush brush(Gdiplus::Color(255, 0, 0, 0));
  Gdiplus::FontFamily fontFamily(L"Times New Roman");
  Gdiplus::Font font(&fontFamily, (float)size, Gdiplus::FontStyleRegular,
                     Gdiplus::UnitPixel);
  Gdiplus::PointF pointF((float)x - overlay.bounds.x,
                         (float)y - overlay.bounds.y);

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

  draw.g->DrawRectangle(&pen, bounds.x - overlay.bounds.x,
                        bounds.y - overlay.bounds.y, bounds.width,
                        bounds.height);
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
  struct Point position;
  int buttons;
  int key;
  bool shift;
};

struct Input oldInput;
struct Input newInput;

typedef void (*BinOnDrawFn)(struct Bin *bin);
typedef void (*BinOnInputFn)(struct Bin *bin);

struct Bin {
  BinOnDrawFn onDrawFn;
  BinOnInputFn onInputFn;

  struct Bounds bounds;

  struct Bin **Subs;
};

void AddSubBin(struct Bin *Bin, struct Bin *Sub) {}

void RemoveSubBin(struct Bin *Bin, struct Bin *Sub) {}

struct Shelf {
  struct Bin bin;

  int rows;
  int cols;

  int hoverRow;
  int hoverCol;
};

#define OffsetOf(type_, member_)                                               \
  (size_t)((ptrdiff_t) &                                                       \
           reinterpret_cast<const volatile char &>((((type_ *)0)->member_)))
#define Unwrap(type_, member_, ptr_)                                           \
  (type_ *)((char *)ptr_ - OffsetOf(type_, member_))
#define Wrap(ptr_, member_) (&((ptr_)->member_))

struct Bounds ShelfMakeCellBounds(struct Shelf *shelf, int row, int col) {
  struct Bounds bounds;
  bounds.width = (shelf->bin.bounds.width - (shelf->cols + 1) * BORDER_INSET) /
                 shelf->cols;
  bounds.height =
      (shelf->bin.bounds.height - (shelf->rows + 1) * BORDER_INSET) /
      shelf->rows;
  bounds.x =
      shelf->bin.bounds.x + col * (bounds.width + BORDER_INSET) + BORDER_INSET;
  bounds.y =
      shelf->bin.bounds.y + row * (bounds.height + BORDER_INSET) + BORDER_INSET;
  return bounds;
}

void ShelfDraw(struct Bin *bin) {
  struct Shelf *shelf = Unwrap(struct Shelf, bin, bin);

  for (int row = 0; row < shelf->rows; row++) {
    for (int col = 0; col < shelf->cols; col++) {
      struct Bounds bounds = ShelfMakeCellBounds(shelf, row, col);

      bool dashed = false;
      if (col == shelf->hoverCol && row == shelf->hoverRow)
        dashed = true;

      DrawRectangle(bounds, BORDER_WIDTH, dashed);
    }
  }
}

void ShelfInput(struct Bin *bin) {
  struct Shelf *shelf = Unwrap(struct Shelf, bin, bin);

  if (newInput.key == 'C' && !newInput.shift)
    shelf->cols++;
  if (newInput.key == 'C' && newInput.shift && shelf->cols > 1)
    shelf->cols--;

  if (newInput.key == 'R' && !newInput.shift)
    shelf->rows++;
  if (newInput.key == 'R' && newInput.shift && shelf->rows > 1)
    shelf->rows--;

  shelf->hoverRow = -1;
  shelf->hoverCol = -1;
  for (int row = 0; row < shelf->rows; row++) {
    for (int col = 0; col < shelf->cols; col++) {
      struct Bounds bounds = ShelfMakeCellBounds(shelf, row, col);

      if (PointInBounds(newInput.position, bounds)) {
        shelf->hoverRow = row;
        shelf->hoverCol = col;

        onDeck.placement = bounds;
      }
    }
  }
}

struct Shelf *NewShelf() {
  struct Shelf *shelf = AllocateStruct(struct Shelf);
  shelf->bin.onDrawFn = ShelfDraw;
  shelf->bin.onInputFn = ShelfInput;
  shelf->rows = 1;
  shelf->cols = 1;
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
  monitor->bounds.width =
      monitor->info.rcWork.right - monitor->info.rcWork.left;
  monitor->bounds.height =
      monitor->info.rcWork.bottom - monitor->info.rcWork.top;
}

struct Monitor *GetMonitorAtCursor() {
  POINT mousePoint = {};
  CheckWin32(GetCursorPos(&mousePoint));

  HMONITOR hMonitor = MonitorFromPoint(mousePoint, MONITOR_DEFAULTTONULL);
  if (hMonitor == NULL) {
    ReportError("Mouse position %d %d was not over any monitor", mousePoint.x,
                mousePoint.y);
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

  SetWindowPos(onDeck.hWnd, NULL, onDeck.placement.x, onDeck.placement.y,
               onDeck.placement.width, onDeck.placement.height, SWP_SHOWWINDOW);
}

void ShowOverlay() {
  overlay.monitor = GetMonitorAtCursor();
  if (overlay.monitor == NULL)
    return;

  overlay.bounds = overlay.monitor->bounds;

#if HALF_MONITOR
  overlay.bounds.Width = overlay.bounds.Width / 2;
  overlay.bounds.X += overlay.bounds.Width;
#endif

  SetWindowPos(overlay.hWnd, HWND_TOPMOST, overlay.bounds.x, overlay.bounds.y,
               overlay.bounds.width, overlay.bounds.height, SWP_SHOWWINDOW);

  overlay.monitor->root->bounds = overlay.bounds;

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
    ReportError(
        "Overlay received a mouse event %d at %d %d when it was not open",
        message, x, y);
    return;
  }

  oldInput = newInput;
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
  HBITMAP hbmMem =
      CreateCompatibleBitmap(draw.hdc, rc.right - rc.left, rc.bottom - rc.top);
  SelectObject(hdcMem, hbmMem);

  HBRUSH hbrBkGnd = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
  FillRect(hdcMem, &rc, hbrBkGnd);
  DeleteObject(hbrBkGnd);

  draw.g = new Gdiplus::Graphics(hdcMem);

  overlay.monitor->root->onDrawFn(overlay.monitor->root);

  delete draw.g;
  draw.g = NULL;

  BitBlt(draw.hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
         hdcMem, 0, 0, SRCCOPY);

  DeleteObject(hbmMem);
  DeleteDC(hdcMem);

  EndPaint(overlay.hWnd, &draw.ps);
}

LRESULT CALLBACK OverlayWindowProc(HWND hWnd, UINT message, WPARAM wParam,
                                   LPARAM lParam) {
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
    OnOverlayMouse(message, (UINT)wParam, GET_X_LPARAM(lParam),
                   GET_Y_LPARAM(lParam));
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

  overlay.hWnd = CreateWindow("WINDY_MAIN", "Windy", WS_POPUPWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                              rc.bottom - rc.top, NULL, NULL, win.hInst, NULL);

  SetWindowLong(overlay.hWnd, GWL_EXSTYLE,
                GetWindowLong(overlay.hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
  SetLayeredWindowAttributes(overlay.hWnd, 0, OVERLAY_ALPHA, LWA_ALPHA);

  CheckWin32(RegisterHotKey(overlay.hWnd, HOTKEY_ID, HOTKEY_META, HOTKEY_CODE));
}

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_ HINSTANCE hPrevInstance,
                     _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
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
