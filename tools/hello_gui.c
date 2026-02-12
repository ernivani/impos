/* Minimal Win32 GUI app for testing ImposOS PE loader + Win32 shims.
 *
 * Build with:
 *   i686-w64-mingw32-gcc -o hello_gui.exe hello_gui.c \
 *       -nostdlib -lkernel32 -luser32 -lgdi32 \
 *       -Wl,--subsystem,windows -e _WinMain@16
 */
#include <windows.h>

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            SetBkMode(hdc, TRANSPARENT);

            /* Dark background */
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 30));
            FillRect(hdc, &rc, bgBrush);
            DeleteObject(bgBrush);

            /* Title text */
            SetTextColor(hdc, RGB(255, 255, 255));
            TextOutA(hdc, 20, 20, "Hello from Win32 GUI!", 21);

            /* Subtitle */
            SetTextColor(hdc, RGB(180, 180, 180));
            TextOutA(hdc, 20, 50, "Running on ImposOS", 18);
            TextOutA(hdc, 20, 70, "PE32 loader works!", 18);

            /* Colored rectangles */
            HBRUSH red = CreateSolidBrush(RGB(220, 50, 50));
            HBRUSH green = CreateSolidBrush(RGB(50, 180, 50));
            HBRUSH blue = CreateSolidBrush(RGB(50, 100, 220));
            HBRUSH yellow = CreateSolidBrush(RGB(230, 200, 50));

            RECT r1 = { 20, 100, 100, 160 };
            RECT r2 = { 110, 100, 190, 160 };
            RECT r3 = { 200, 100, 280, 160 };
            RECT r4 = { 290, 100, 370, 160 };

            FillRect(hdc, &r1, red);
            FillRect(hdc, &r2, green);
            FillRect(hdc, &r3, blue);
            FillRect(hdc, &r4, yellow);

            DeleteObject(red);
            DeleteObject(green);
            DeleteObject(blue);
            DeleteObject(yellow);

            /* Label under rectangles */
            SetTextColor(hdc, RGB(140, 140, 140));
            TextOutA(hdc, 20, 170, "GDI drawing test", 16);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "ImposGUI";

    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        0, "ImposGUI", "Win32 on ImposOS",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 250,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
