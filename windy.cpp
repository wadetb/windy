#include <stddef.h>
#include <WindowsX.h>
#include <ShellScalingAPI.h>
#include <GDIPlus.h>

#define HALF_MONITOR 0

#define HOTKEY_ID   1
#define HOTKEY_META MOD_WIN
#define HOTKEY_CODE VK_OEM_3

#define OVERLAY_ALPHA 200

#define BORDER_INSET 4
#define BORDER_WIDTH 8

#define IDR_ICON 1

#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "User32.lib")

struct  
{
    HINSTANCE hInst;
} Win;

struct Rect
{
    int X, Y, Width, Height;
};

struct {
    HWND hWnd;
    struct Rect PlaceRect;
} OnDeck;

struct {
	struct Monitor *Monitor;
    HWND hWnd;
    struct Rect Rect;
    bool IsOpen;
} Overlay;

void FatalWin32Error(const char *Format, ...)
{
}

#define CheckWin32(x_) \
    do { \
        if (!(x_)) { \
            FatalWin32Error("" #x_ " failed"); \
        } \
    } while (0)

void ReportError(const char *Format, ...)
{
}

void FatalError(const char *Format, ...)
{
}

void *Alloc(size_t Size, size_t Count, const char *Name)
{
    void *Mem = calloc(Count, Size);
    if (Mem == NULL)
        FatalError("Allocation of %d %s%s failed", Count, Name, Count>1?"s":"");
    return Mem;
}

#define AllocStruct(type_) (type_ *)Alloc(sizeof(type_), 1, #type_)

struct
{
    PAINTSTRUCT ps;
    HDC hdc;
    Gdiplus::Graphics *g;
} Draw;

void DrawText(int X, int Y, int Size, const char *Text)
{
    Gdiplus::SolidBrush Brush(Gdiplus::Color(255, 0, 0, 0));
    Gdiplus::FontFamily FontFamily(L"Times New Roman");
    Gdiplus::Font Font(&FontFamily, (float)Size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::PointF PointF((float)X - Overlay.Rect.X, (float)Y - Overlay.Rect.Y);

    size_t Chars;
    WCHAR WideText[_MAX_PATH];
    mbstowcs_s(&Chars, WideText, _MAX_PATH, Text, strlen(Text));

    Draw.g->DrawString(WideText, -1, &Font, PointF, &Brush);
}

void DrawRect(struct Rect Rect, int LineWidth, bool Dashed)
{
    Gdiplus::Pen Pen(Gdiplus::Color(255, 0, 0, 0), 10);
    Pen.SetAlignment(Gdiplus::PenAlignmentInset);

    if (Dashed)
        Pen.SetDashStyle(Gdiplus::DashStyleDash);

    Draw.g->DrawRectangle(&Pen, Rect.X - Overlay.Rect.X, Rect.Y - Overlay.Rect.Y, Rect.Width, Rect.Height);
}

struct Input
{
    int X;
    int Y;
    int Buttons;
    int Key;
	bool Shift;
};

struct Input Old;
struct Input New;

typedef void (*BinOnDrawFn)(struct Bin *Bin);
typedef void (*BinOnInputFn)(struct Bin *Bin);

struct Bin
{
    BinOnDrawFn OnDrawFn;
    BinOnInputFn OnInputFn;

    struct Rect Rect;

    struct Bin **Subs;
};

void AddSubBin(struct Bin *Bin, struct Bin *Sub)
{
}

void RemoveSubBin(struct Bin *Bin, struct Bin *Sub)
{
}

struct Shelf
{
    struct Bin Bin;
    
    int Rows;
    int Cols;

    int HoverRow;
    int HoverCol;
};

#define OffsetOf(type_, member_) (size_t)( (ptrdiff_t)&reinterpret_cast<const volatile char&>((((type_ *)0)->member_)) )
#define Unwrap(type_, member_, ptr_) (type_ *)((char *)ptr_ - OffsetOf(type_, member_))

void ShelfDraw(struct Bin *Bin)
{
    struct Shelf *Shelf = Unwrap(struct Shelf, Bin, Bin);

    for (int Row = 0; Row < Shelf->Rows; Row++)
    {
        for (int Col = 0; Col < Shelf->Cols; Col++)
        {
            struct Rect CellRect;
            CellRect.Width = (Shelf->Bin.Rect.Width - (Shelf->Cols + 1) * BORDER_INSET) / Shelf->Cols;
            CellRect.Height = (Shelf->Bin.Rect.Height - (Shelf->Rows + 1) * BORDER_INSET) / Shelf->Rows;
            CellRect.X = Shelf->Bin.Rect.X + Col * (CellRect.Width + BORDER_INSET) + BORDER_INSET;
            CellRect.Y = Shelf->Bin.Rect.Y + Row * (CellRect.Height + BORDER_INSET) + BORDER_INSET;

            bool Dashed = false;
            if (Col == Shelf->HoverCol && Row == Shelf->HoverRow)
                Dashed = true;

            DrawRect(CellRect, BORDER_WIDTH, Dashed);
        }
    }
}

void ShelfInput(struct Bin *Bin)
{
    struct Shelf *Shelf = Unwrap(struct Shelf, Bin, Bin);

    if (New.Key == 'C' && !New.Shift)
        Shelf->Cols++;
    if (New.Key == 'C' && New.Shift && Shelf->Cols > 1)
        Shelf->Cols--;
    
	if (New.Key == 'R' && !New.Shift)
		Shelf->Rows++;
	if (New.Key == 'R' && New.Shift && Shelf->Rows > 1)
		Shelf->Rows--;

    Shelf->HoverRow = -1;
    Shelf->HoverCol = -1;
    for (int Row = 0; Row < Shelf->Rows; Row++)
    {
        for (int Col = 0; Col < Shelf->Cols; Col++)
        {
            struct Rect CellRect;
            CellRect.Width = (Shelf->Bin.Rect.Width - (Shelf->Cols + 1) * BORDER_INSET) / Shelf->Cols;
            CellRect.Height = (Shelf->Bin.Rect.Height - (Shelf->Rows + 1) * BORDER_INSET) / Shelf->Rows;
            CellRect.X = Shelf->Bin.Rect.X + Col * (CellRect.Width + BORDER_INSET) + BORDER_INSET;
            CellRect.Y = Shelf->Bin.Rect.Y + Row * (CellRect.Height + BORDER_INSET) + BORDER_INSET;

            if (New.X >= CellRect.X && New.Y < CellRect.X + CellRect.Width && 
                New.Y >= CellRect.Y && New.Y < CellRect.Y + CellRect.Height)
            {
                Shelf->HoverRow = Row;
                Shelf->HoverCol = Col;

                OnDeck.PlaceRect = CellRect;
            }
        }
    }
}

struct Shelf *NewShelf()
{
    struct Shelf *Shelf = AllocStruct(struct Shelf);
    Shelf->Bin.OnDrawFn = ShelfDraw;
    Shelf->Bin.OnInputFn = ShelfInput;
    Shelf->Rows = 1;
    Shelf->Cols = 1;
    return Shelf;
}

#define MONITOR_LIMIT 16

struct Monitor
{
	HMONITOR hMonitor;
	MONITORINFO Info;
	struct Rect Rect;
	struct Bin *Root;
};

struct Monitor Monitors[MAX_MONITORS];

void UpdateMonitorInfo(struct Monitor *Monitor)
{
	Monitor->Info = { sizeof(MONITORINFO) };
	CheckWin32(GetMonitorInfo(Monitor->hMonitor, &Monitor->Info));

	Monitor->Rect.X = Monitor->Info.rcWork.left;
	Monitor->Rect.Y = Monitor->Info.rcWork.top;
	Monitor->Rect.Width = Monitor->Info.rcWork.right - Monitor->Info.rcWork.left;
	Monitor->Rect.Height = Monitor->Info.rcWork.bottom - Monitor->Info.rcWork.top;
}

struct Monitor *GetMonitorAtCursor()
{
	POINT MousePt = {};
	CheckWin32(GetCursorPos(&MousePt));

	HMONITOR hMonitor = MonitorFromPoint(MousePt, MONITOR_DEFAULTTONULL);
	if (hMonitor == NULL)
	{
		ReportError("Mouse position %d %d was not over any monitor", MousePt.x, MousePt.y);
		return NULL;
	}

	for (int i = 0; i < MONITOR_LIMIT; i++)
	{
		struct Monitor *Monitor = &Monitors[i];
		if (Monitor->hMonitor == NULL)
		{
			Monitor->hMonitor = hMonitor;
			Monitor->Root = &NewShelf()->Bin;
			UpdateMonitorInfo(Monitor);
			return Monitor;
		}
		if (Monitor->hMonitor == hMonitor)
		{
			UpdateMonitorInfo(Monitor);
			return Monitor;
		}
	}

	return NULL;
}

void PickOnDeckWindow()
{
    POINT MousePt = {};
    CheckWin32(GetCursorPos(&MousePt));

    OnDeck.PlaceRect = { 100, 100, 800, 600 };

	HWND hWnd = WindowFromPoint(MousePt);
	OnDeck.hWnd = GetAncestor(hWnd, GA_ROOT);
}

void ClearOnDeckWindow()
{
    OnDeck.hWnd = NULL;
}

void PlaceOnDeckWindow()
{
    if (!OnDeck.hWnd)
    {
        ReportError("Tried to place the on deck window when none was active");
        return;
    }

    SetWindowPos(OnDeck.hWnd, NULL, OnDeck.PlaceRect.X, OnDeck.PlaceRect.Y, OnDeck.PlaceRect.Width, OnDeck.PlaceRect.Height, SWP_SHOWWINDOW);
}

void ShowOverlay()
{
	Overlay.Monitor = GetMonitorAtCursor();
	if (Overlay.Monitor == NULL)
		return;

	Overlay.Rect = Overlay.Monitor->Rect;

#if HALF_MONITOR
    Overlay.Rect.Width = Overlay.Rect.Width / 2;
    Overlay.Rect.X += Overlay.Rect.Width;
#endif

    SetWindowPos(Overlay.hWnd, HWND_TOPMOST, Overlay.Rect.X, Overlay.Rect.Y, Overlay.Rect.Width, Overlay.Rect.Height, SWP_SHOWWINDOW);

    Overlay.Monitor->Root->Rect = Overlay.Rect;

    Overlay.IsOpen = true;
}

void HideOverlay()
{
    ShowWindow(Overlay.hWnd, SW_HIDE);

    Overlay.IsOpen = false;
}

void OnOverlayHotkey()
{
    if (!Overlay.IsOpen)
    {
        PickOnDeckWindow();
        ShowOverlay();
    }
    else
    {
        HideOverlay();
        ClearOnDeckWindow();
    }
}

void OnOverlayMouse(UINT Message, UINT Buttons, int X, int Y)
{
    if (!Overlay.IsOpen)
    {
        ReportError("Overlay received a mouse event %d at %d %d when it was not open", Message, X, Y);
        return;
    }

    Old = New;
    New.X = X + Overlay.Rect.X;
    New.Y = Y + Overlay.Rect.Y;
    New.Buttons = Buttons;
    New.Key = 0;

	Overlay.Monitor->Root->OnInputFn(Overlay.Monitor->Root);

    InvalidateRect(Overlay.hWnd, NULL, TRUE);

    if (Message == WM_LBUTTONUP)
    {
        HideOverlay();
        PlaceOnDeckWindow();
    }
}

void OnOverlayKey(UINT Key)
{
	if (!Overlay.IsOpen)
	{
		ReportError("Overlay received a key event %d when it was not open", Key);
		return;
	}

	Old = New;
    New.Key = Key;
	New.Shift = GetAsyncKeyState(VK_SHIFT) || GetAsyncKeyState(VK_LSHIFT);

	Overlay.Monitor->Root->OnInputFn(Overlay.Monitor->Root);

    InvalidateRect(Overlay.hWnd, NULL, TRUE);

    if (Key == VK_ESCAPE)
    {
        HideOverlay();
        ClearOnDeckWindow();
    }
}

void OnOverlayPaint()
{
	if (!Overlay.IsOpen)
	{
		ReportError("Overlay received a paint event");
		return;
	}

	Draw.hdc = BeginPaint(Overlay.hWnd, &Draw.ps);

    RECT rc;
    GetClientRect(Overlay.hWnd, &rc);

    HDC hdcMem  = CreateCompatibleDC(Draw.hdc);
    HBITMAP hbmMem = CreateCompatibleBitmap(Draw.hdc, rc.right - rc.left, rc.bottom - rc.top);
    SelectObject(hdcMem, hbmMem);

    HBRUSH hbrBkGnd = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    FillRect(hdcMem, &rc, hbrBkGnd);
    DeleteObject(hbrBkGnd);

    Draw.g = new Gdiplus::Graphics(hdcMem);

	Overlay.Monitor->Root->OnDrawFn(Overlay.Monitor->Root);

    delete Draw.g;
    Draw.g = NULL;

    BitBlt(Draw.hdc, rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, hdcMem, 0, 0, SRCCOPY);

    DeleteObject(hbmMem);
    DeleteDC(hdcMem);

    EndPaint(Overlay.hWnd, &Draw.ps); 
}

LRESULT CALLBACK OverlayWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
    case WM_HOTKEY:
        switch (LOWORD(wParam))
        {
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

void CreateOverlay()
{
    WNDCLASSEX wcex;
    ZeroMemory(&wcex, sizeof(wcex));
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = OverlayWindowProc;
    wcex.hInstance = Win.hInst;
    wcex.hIcon = LoadIcon(Win.hInst, MAKEINTRESOURCE(IDR_ICON));
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = "WINDY_MAIN";
    CheckWin32(RegisterClassEx(&wcex));

    RECT rc = { 0, 0, 100, 100};
    CheckWin32(AdjustWindowRect(&rc, WS_POPUP, FALSE));

    Overlay.hWnd = CreateWindow("WINDY_MAIN", "Windy", WS_POPUPWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, Win.hInst, NULL);

    SetWindowLong(Overlay.hWnd, GWL_EXSTYLE, GetWindowLong(Overlay.hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(Overlay.hWnd, 0, OVERLAY_ALPHA, LWA_ALPHA);

    CheckWin32(RegisterHotKey(Overlay.hWnd, HOTKEY_ID, HOTKEY_META, HOTKEY_CODE));
}

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    SetProcessDpiAwareness((PROCESS_DPI_AWARENESS)PROCESS_PER_MONITOR_DPI_AWARE);


    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    CreateOverlay();

    for (;;)
    {
        MSG Msg;
        BOOL Result = GetMessage(&Msg, NULL, 0, 0);
        if (Result < 0)
            FatalWin32Error("GetMessage failed");
        if (Result == 0)
            break;
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);

    return 0;
}
