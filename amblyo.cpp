#include "windows.h"
#include "strsafe.h"
#include "magnification.h"
#include "resource.h"
#include <math.h>
#include <Commctrl.h>



#define RESTOREDWINDOWSTYLES WS_SIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN | WS_CAPTION | WS_MAXIMIZEBOX


const LPCWSTR AppTitle = L"Amblyo";


struct Amblyo {
    HWND mainWindowHandle;
    RECT mainWindowRect;

    HWND magWindowLeftHandle;
    RECT magWindowLeftRect;
    
    HWND magWindowRightHandle;    
    RECT magWindowRightRect; 

    UINT_PTR magUpdateTimerId;

    FLOAT leftHueAngle;
    FLOAT leftSaturation;
    FLOAT leftValue;
    FLOAT rightHueAngle;
    FLOAT rightSaturation;
    FLOAT rightValue;

    BOOL isFullScreen;
};
Amblyo amblyo;


LRESULT CALLBACK HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ColorAdjustmentDialogProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);
void CALLBACK UpdateMagWindow(HWND /*hwnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/);
void GoPartialScreen();
void GoFullScreen();


void UpdateLeftMagRect() {
    GetClientRect(amblyo.mainWindowHandle, &amblyo.magWindowLeftRect);
    LONG width = amblyo.magWindowLeftRect.right - amblyo.magWindowLeftRect.left;
    amblyo.magWindowLeftRect.right -= width / 2;
}


void UpdateRightMagRect() {
    GetClientRect(amblyo.mainWindowHandle, &amblyo.magWindowRightRect);
    LONG width = amblyo.magWindowRightRect.right - amblyo.magWindowRightRect.left;
    amblyo.magWindowRightRect.left += width / 2;
}


MAGCOLOREFFECT HSVTransform(FLOAT hueAngle, FLOAT s, FLOAT v) {
    FLOAT phi = hueAngle * 0.0174533f;
    FLOAT u = cosf(phi);
    FLOAT w = sinf(phi);
    FLOAT vs = v * s;
    FLOAT vsu = vs * u;
    FLOAT vsw = vs * w;

    return {{
            {0.299f*v + 0.701f*vsu + 0.168f*vsw, 0.587f*v - 0.587f*vsu + 0.330f*vsw, 0.114f*v - 0.114f*vsu - 0.497f*vsw, 0, 0},
            {0.299f*v - 0.299f*vsu - 0.328f*vsw, 0.587f*v + 0.413f*vsu + 0.035f*vsw, 0.114f*v - 0.114f*vsu + 0.292f*vsw, 0, 0},
            {0.299f*v - 0.3f*vsu + 1.25f*vsw, 0.587f*v - 0.588f*vsu - 1.05f*vsw, 0.114f*v + 0.886f*vsu - 0.203f*vsw, 0, 0},
            {0, 0, 0, 1, 0},
            {0, 0, 0, 0, 1}
    }};
}


Amblyo InitializeAmblyo(HINSTANCE hInstance) {
    Amblyo result = {};

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

    result.leftHueAngle = 0.0f;
    result.leftSaturation = 1.0f;
    result.leftValue = 1.0f;
    result.rightHueAngle = 0.0f;
    result.rightSaturation = 1.0f;
    result.rightValue = 1.0f;

    result.mainWindowHandle = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED, 
        WindowClassName, WindowTitle, 
        RESTOREDWINDOWSTYLES,
        result.mainWindowRect.top, result.mainWindowRect.left, result.mainWindowRect.right, result.mainWindowRect.bottom, NULL, NULL, hInstance, NULL);    

    SetLayeredWindowAttributes(result.mainWindowHandle, 0, 255, LWA_ALPHA);

    UpdateLeftMagRect();
    result.magWindowLeftHandle = CreateWindow(WC_MAGNIFIER, TEXT("MagnifierWindow"), 
        WS_CHILD | MS_SHOWMAGNIFIEDCURSOR | WS_VISIBLE,
        result.magWindowLeftRect.left, result.magWindowLeftRect.top, result.magWindowLeftRect.right, result.magWindowLeftRect.bottom, result.mainWindowHandle, NULL, hInstance, NULL );
    
    UpdateRightMagRect();
    result.magWindowRightHandle = CreateWindow(WC_MAGNIFIER, TEXT("MagnifierWindow"), 
        WS_CHILD | MS_SHOWMAGNIFIEDCURSOR | WS_VISIBLE,
        result.magWindowRightRect.left, result.magWindowRightRect.top, result.magWindowRightRect.right, result.magWindowRightRect.bottom, result.mainWindowHandle, NULL, hInstance, NULL );

    const UINT timerInterval = 16;
    result.magUpdateTimerId = SetTimer(result.magWindowLeftHandle, 0, 16, UpdateMagWindow);

    return result;
}


int APIENTRY WinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE /*hPrevInstance*/,
                     _In_ LPSTR /*lpCmdLine*/,
                     _In_ int nCmdShow)
{
    if(MagInitialize() == FALSE) {
        MessageBox(NULL, L"Failed to initialize Magnification API", AppTitle, MB_OK);
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
    switch (message) {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (amblyo.isFullScreen) {
                GoPartialScreen();
            }
        }
        else if (wParam == 'C') {
            DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_DIALOG1), hWnd, ColorAdjustmentDialogProc);
        }
        else if(wParam == 'L') {
            MAGCOLOREFFECT effect = HSVTransform(0.0f, 1.0f, 1.0f);
            MagSetColorEffect(amblyo.magWindowLeftHandle, &effect);        
        }
        else if(wParam == 'R') {
            MAGCOLOREFFECT effect = HSVTransform(0.0f, 1.0f, 1.0f);
            MagSetColorEffect(amblyo.magWindowRightHandle, &effect);        
        }
        break;

    case WM_SYSCOMMAND:
        if (GET_SC_WPARAM(wParam) == SC_MAXIMIZE) {
            GoFullScreen();
        }
        else {
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
        
    case WM_SIZE:
        if ( amblyo.magWindowLeftHandle != NULL ) {
            UpdateLeftMagRect();
            UpdateRightMagRect();
            // Resize the control to fill the window.
            SetWindowPos(amblyo.magWindowLeftHandle, NULL, 
                amblyo.magWindowLeftRect.left, amblyo.magWindowLeftRect.top, amblyo.magWindowLeftRect.right, amblyo.magWindowLeftRect.bottom, 0);
            SetWindowPos(amblyo.magWindowRightHandle, NULL, 
                amblyo.magWindowRightRect.left, amblyo.magWindowRightRect.top, amblyo.magWindowRightRect.right, amblyo.magWindowRightRect.bottom, 0);
        }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0; 
}


INT_PTR CALLBACK ColorAdjustmentDialogProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam) {
    switch (Message)
    {
    case WM_INITDIALOG:
        {
            HWND slider = GetDlgItem(hwnd, IDC_HUESLIDER_LEFT);
            SendMessage(slider , TBM_SETRANGE, FALSE, MAKELONG(0, 360));
            SendMessage(slider, TBM_SETPOS, TRUE, (LONG)amblyo.leftHueAngle);

            slider = GetDlgItem(hwnd, IDC_SATSLIDER_LEFT);
            SendMessage(slider, TBM_SETRANGE, FALSE, MAKELONG(100, 1000));
            SendMessage(slider, TBM_SETPOS, TRUE, (LONG)(amblyo.leftSaturation*100.0));

            slider = GetDlgItem(hwnd, IDC_VALSLIDER_LEFT);
            SendMessage(slider, TBM_SETRANGE, FALSE, MAKELONG(100, 1000));
            SendMessage(slider, TBM_SETPOS, TRUE, (LONG)(amblyo.leftValue*100.0));

            slider = GetDlgItem(hwnd, IDC_HUESLIDER_RIGHT);
            SendMessage(slider, TBM_SETRANGE, FALSE, MAKELONG(0, 360));
            SendMessage(slider, TBM_SETPOS, TRUE, (LONG)amblyo.rightHueAngle);

            slider = GetDlgItem(hwnd, IDC_SATSLIDER_RIGHT);
            SendMessage(slider, TBM_SETRANGE, FALSE, MAKELONG(100, 1000));
            SendMessage(slider, TBM_SETPOS, TRUE, (LONG)(amblyo.rightSaturation*100.0));

            slider = GetDlgItem(hwnd, IDC_VALSLIDER_RIGHT);
            SendMessage(slider, TBM_SETRANGE, FALSE, MAKELONG(100, 1000));
            SendMessage(slider, TBM_SETPOS, TRUE, (LONG)(amblyo.rightValue*100.0));
        }
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            EndDialog(hwnd, IDOK);
            break;
        }
        break;
    case WM_HSCROLL:
        {
            HWND slider = (HWND)lParam;
            int id = GetDlgCtrlID(slider);
            int pos = SendMessage(slider, TBM_GETPOS, 0, 0);
            switch (id) {
            case IDC_HUESLIDER_LEFT:
                amblyo.leftHueAngle = (float)pos;
                break;
            case IDC_SATSLIDER_LEFT:
                amblyo.leftSaturation = ((float)pos)/100.0f;
                break;
            case IDC_VALSLIDER_LEFT:
                amblyo.leftValue = ((float)pos)/100.0f;
                break;
            case IDC_HUESLIDER_RIGHT:
                amblyo.rightHueAngle = (float)pos;
                break;
            case IDC_SATSLIDER_RIGHT:
                amblyo.rightSaturation = ((float)pos) / 100.0f;
                break;
            case IDC_VALSLIDER_RIGHT:
                amblyo.rightValue = ((float)pos) / 100.0f;
                break;
            };
        }
        break;
    default:
        return FALSE;
    }
    return TRUE;
}



void CALLBACK UpdateMagWindow(HWND /*hwnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/)
{
    RECT clientRect;
    RECT windowRect;
    GetClientRect(amblyo.mainWindowHandle, &clientRect);
    GetWindowRect(amblyo.mainWindowHandle, &windowRect);

    RECT screenClient;
    {
        POINT screenCoords;
        screenCoords.x = clientRect.left;
        screenCoords.y = clientRect.top;
        ClientToScreen(amblyo.mainWindowHandle, &screenCoords);

        screenClient.left = screenCoords.x;
        screenClient.top = screenCoords.y;

        screenCoords.x = clientRect.right;
        screenCoords.y = clientRect.bottom;
        ClientToScreen(amblyo.mainWindowHandle, &screenCoords);

        screenClient.right = screenCoords.x;
        screenClient.bottom = screenCoords.y;
    }

    LONG width =(screenClient.right - screenClient.left);
    LONG height = (screenClient.bottom - screenClient.top);
    RECT sourceLeftRect = screenClient;
    sourceLeftRect.right -= width / 2;
    RECT sourceRightRect = screenClient;
    sourceRightRect.left = sourceLeftRect.right;
    
    // Set the source rectangle for the magnifier control.
    MagSetWindowSource(amblyo.magWindowLeftHandle, sourceLeftRect);
    MagSetWindowSource(amblyo.magWindowRightHandle, sourceRightRect);

    // Reclaim topmost status, to prevent unmagnified menus from remaining in view. 
    SetWindowPos(amblyo.mainWindowHandle, HWND_TOPMOST, 0, 0, 0, 0, 
        SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE );

    MAGCOLOREFFECT effect = HSVTransform(amblyo.leftHueAngle, amblyo.leftSaturation, amblyo.leftValue);
    MagSetColorEffect(amblyo.magWindowLeftHandle, &effect);

    effect = HSVTransform(amblyo.rightHueAngle, amblyo.rightSaturation, amblyo.rightValue);
    MagSetColorEffect(amblyo.magWindowRightHandle, &effect);

    // Force redraw.
    InvalidateRect(amblyo.magWindowLeftHandle, NULL, TRUE);
    InvalidateRect(amblyo.magWindowRightHandle, NULL, TRUE);
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


void GoPartialScreen()
{
    amblyo.isFullScreen = FALSE;

    SetWindowLong(amblyo.mainWindowHandle, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED);
    SetWindowLong(amblyo.mainWindowHandle, GWL_STYLE, RESTOREDWINDOWSTYLES);
    SetWindowPos(amblyo.mainWindowHandle, HWND_TOPMOST, 
        amblyo.mainWindowRect.left, amblyo.mainWindowRect.top, amblyo.mainWindowRect.right, amblyo.mainWindowRect.bottom, 
        SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOACTIVATE);
}