#include "windows.h"
#include "strsafe.h"
#include "magnification.h"


#define RESTOREDWINDOWSTYLES WS_SIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN | WS_CAPTION | WS_MAXIMIZEBOX


const LPCSTR AppTitle = "Amblyo";


struct Amblyo {
    HWND mainWindowHandle;
    RECT mainWindowRect;
    HWND magWindowHandle;
    RECT magWindowRect;
    UINT_PTR magUpdateTimerId;
    FLOAT magFactor;
    BOOL isFullScreen;
};
Amblyo amblyo;


LRESULT CALLBACK HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void CALLBACK UpdateMagWindow(HWND /*hwnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/);
void GoPartialScreen();
void GoFullScreen();

Amblyo InitializeAmblyo(HINSTANCE hInstance) {
    Amblyo result;

    result.isFullScreen = FALSE;

    const TCHAR WindowClassName[]= TEXT("AmblyoWindow");
    const TCHAR WindowTitle[]= TEXT("Amblyo");    
    { // Register window class for the host window.
        WNDCLASSEX wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEX); 
        wcex.style          = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc    = HostWndProc;
        wcex.hInstance      = hInstance;
        wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground  = (HBRUSH)(1 + COLOR_BTNFACE);
        wcex.lpszClassName  = WindowClassName;
        RegisterClassEx(&wcex);
    }

    result.mainWindowRect.top = 0;
    result.mainWindowRect.bottom = GetSystemMetrics(SM_CYSCREEN) / 4;  // top quarter of screen
    result.mainWindowRect.left = 0;
    result.mainWindowRect.right = GetSystemMetrics(SM_CXSCREEN);

    result.mainWindowHandle = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED, 
        WindowClassName, WindowTitle, 
        RESTOREDWINDOWSTYLES,
        result.mainWindowRect.top, result.mainWindowRect.left, result.mainWindowRect.right, result.mainWindowRect.bottom, NULL, NULL, hInstance, NULL);    

    SetLayeredWindowAttributes(result.mainWindowHandle, 0, 255, LWA_ALPHA);

    GetClientRect(result.mainWindowHandle, &result.magWindowRect);
    result.magWindowHandle = CreateWindow(WC_MAGNIFIER, TEXT("MagnifierWindow"), 
        WS_CHILD | MS_SHOWMAGNIFIEDCURSOR | WS_VISIBLE,
        result.magWindowRect.left, result.magWindowRect.top, result.magWindowRect.right, result.magWindowRect.bottom, result.mainWindowHandle, NULL, hInstance, NULL );

    result.magFactor = 2.0f;

    MAGTRANSFORM matrix;
    memset(&matrix, 0, sizeof(matrix));
    matrix.v[0][0] = result.magFactor;
    matrix.v[1][1] = result.magFactor;
    matrix.v[2][2] = 1.0f;

    MagSetWindowTransform(result.magWindowHandle, &matrix);

    const UINT timerInterval = 16;
    result.magUpdateTimerId = SetTimer(result.magWindowHandle, 0, 16, UpdateMagWindow);


    return result;
}


int APIENTRY WinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE /*hPrevInstance*/,
                     _In_ LPSTR /*lpCmdLine*/,
                     _In_ int nCmdShow)
{
    if(MagInitialize() == FALSE) {
        MessageBox(NULL, "Failed to initialize Magnification API", AppTitle, MB_OK);
        return 1;
    }

    amblyo = InitializeAmblyo(hInstance);

    ShowWindow(amblyo.mainWindowHandle, nCmdShow);
    UpdateWindow(amblyo.mainWindowHandle);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}


LRESULT CALLBACK HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) 
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            if (amblyo.isFullScreen) 
            {
                GoPartialScreen();
            }
        }
        break;

    case WM_SYSCOMMAND:
        if (GET_SC_WPARAM(wParam) == SC_MAXIMIZE)
        {
            GoFullScreen();
        }
        else
        {
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;

    case WM_SIZE:
        if ( amblyo.magWindowHandle != NULL )
        {
            GetClientRect(hWnd, &amblyo.magWindowRect);
            // Resize the control to fill the window.
            SetWindowPos(amblyo.magWindowHandle, NULL, 
                amblyo.magWindowRect.left, amblyo.magWindowRect.top, amblyo.magWindowRect.right, amblyo.magWindowRect.bottom, 0);
        }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0; 
}

void CALLBACK UpdateMagWindow(HWND /*hwnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/)
{
    POINT mousePoint;
    GetCursorPos(&mousePoint);

    int width = (int)((amblyo.magWindowRect.right - amblyo.magWindowRect.left) / amblyo.magFactor);
    int height = (int)((amblyo.magWindowRect.bottom - amblyo.magWindowRect.top) / amblyo.magFactor);
    RECT sourceRect;
    sourceRect.left = mousePoint.x - width / 2;
    sourceRect.top = mousePoint.y -  height / 2;

    // Don't scroll outside desktop area.
    if (sourceRect.left < 0)
    {
        sourceRect.left = 0;
    }
    if (sourceRect.left > GetSystemMetrics(SM_CXSCREEN) - width)
    {
        sourceRect.left = GetSystemMetrics(SM_CXSCREEN) - width;
    }
    sourceRect.right = sourceRect.left + width;

    if (sourceRect.top < 0)
    {
        sourceRect.top = 0;
    }
    if (sourceRect.top > GetSystemMetrics(SM_CYSCREEN) - height)
    {
        sourceRect.top = GetSystemMetrics(SM_CYSCREEN) - height;
    }
    sourceRect.bottom = sourceRect.top + height;

    // Set the source rectangle for the magnifier control.
    MagSetWindowSource(amblyo.magWindowHandle, sourceRect);

    // Reclaim topmost status, to prevent unmagnified menus from remaining in view. 
    SetWindowPos(amblyo.mainWindowHandle, HWND_TOPMOST, 0, 0, 0, 0, 
        SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE );

    // Force redraw.
    InvalidateRect(amblyo.magWindowHandle, NULL, TRUE);
}

void GoFullScreen()
{
    amblyo.isFullScreen = TRUE;
    // The window must be styled as layered for proper rendering. 
    // It is styled as transparent so that it does not capture mouse clicks.
    SetWindowLong(amblyo.mainWindowHandle, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    // Give the window a system menu so it can be closed on the taskbar.
    SetWindowLong(amblyo.mainWindowHandle, GWL_STYLE,  WS_CAPTION | WS_SYSMENU);

    // Calculate the span of the display area.
    HDC hDC = GetDC(NULL);
    int xSpan = GetSystemMetrics(SM_CXSCREEN);
    int ySpan = GetSystemMetrics(SM_CYSCREEN);
    ReleaseDC(NULL, hDC);

    // Calculate the size of system elements.
    int xBorder = GetSystemMetrics(SM_CXFRAME);
    int yCaption = GetSystemMetrics(SM_CYCAPTION);
    int yBorder = GetSystemMetrics(SM_CYFRAME);

    // Calculate the window origin and span for full-screen mode.
    int xOrigin = -xBorder;
    int yOrigin = -yBorder - yCaption;
    xSpan += 2 * xBorder;
    ySpan += 2 * yBorder + yCaption;

    SetWindowPos(amblyo.mainWindowHandle, HWND_TOPMOST, xOrigin, yOrigin, xSpan, ySpan, 
        SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOACTIVATE);
}

//
// FUNCTION: GoPartialScreen()
//
// PURPOSE: Makes the host window resizable and focusable.
//
void GoPartialScreen()
{
    amblyo.isFullScreen = FALSE;

    SetWindowLong(amblyo.mainWindowHandle, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED);
    SetWindowLong(amblyo.mainWindowHandle, GWL_STYLE, RESTOREDWINDOWSTYLES);
    SetWindowPos(amblyo.mainWindowHandle, HWND_TOPMOST, 
        amblyo.mainWindowRect.left, amblyo.mainWindowRect.top, amblyo.mainWindowRect.right, amblyo.mainWindowRect.bottom, 
        SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOACTIVATE);
}