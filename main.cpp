#include <windows.h>
#include <shellapi.h>
#include <algorithm>

#include "DebugConsole.h"

#include "resource.h"

namespace
{ 
  constexpr UINT WM_TRAY_ICON    = WM_APP + 1;
  constexpr UINT ID_TRAY_TOGGLE  = 1001;
  constexpr UINT ID_TRAY_EXIT    = 1002;
  constexpr UINT ID_TRAY_ICON_ID = 1;

  constexpr int MIN_WIDTH        = 120;
  constexpr int MIN_HEIGHT       = 80;

  const wchar_t* className = L"EasyWindowsMessageWindow";

  enum class DragMode { None, Move, Resize };

  HINSTANCE g_instance       = nullptr;
  HWND      g_messageWindow  = nullptr;
  HHOOK     g_mouseHook      = nullptr;
  
  HICON g_enabledIcon        = nullptr;
  HICON g_disabledIcon       = nullptr;
  
  NOTIFYICONDATAW g_trayIcon {};

  bool g_enabled = true;
  
  DragMode g_dragMode = DragMode::None;

  HWND  g_targetWindow = nullptr;
  POINT g_startMouse {};
  RECT  g_startRect {};
  
  bool g_resizeFromLeft = false;
  bool g_resizeFromTop = false;
  
  void StopDrag();

  bool IsShiftDown()
  {
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  }

  //
  // Get and validate target window
  HWND GetTargetWindowAtCursor(POINT point)
  {
    HWND hwnd = WindowFromPoint(point);

    if (!hwnd)
      return nullptr;

    // Resolve a child control to its top-level application window.
    hwnd = ::GetAncestor(hwnd, GA_ROOT);

    if (!hwnd || hwnd == g_messageWindow) 
      return nullptr;
    
    if (!::IsWindow(hwnd) || !::IsWindowVisible(hwnd) || hwnd == ::GetDesktopWindow())
    {
      return nullptr;
    }

    // TODO: this may not be necessary, due to checks above
    DWORD processId = 0;
    ::GetWindowThreadProcessId(hwnd, &processId);

    if (processId == ::GetCurrentProcessId())
      return nullptr;

    return hwnd;
  }

  //
  // Setup initial drag state
  bool BeginDrag(DragMode mode)
  {
    POINT cursor{};

    if (!GetCursorPos(&cursor))
      return false;

    auto target = GetTargetWindowAtCursor(cursor);

    if (!target)
      return false;

    RECT rect{};

    if (!GetWindowRect(target, &rect))
      return false;

    g_targetWindow = target;
    g_startMouse = cursor;
    g_startRect = rect;
    g_dragMode = mode;

    if (mode == DragMode::Resize)
    {
      //
      // Determine which sides of the bounding rectangle to modify.
      //
      const int midX = rect.left + (rect.right - rect.left) / 2;
      const int midY = rect.top + (rect.bottom - rect.top) / 2;

      g_resizeFromLeft = cursor.x < midX;
      g_resizeFromTop = cursor.y < midY;
    }

    return true;
  }
  
  //
  // Continuing drag operation - called from hook
  void UpdateDrag(POINT cursor)
  {
    if (g_dragMode == DragMode::None || !g_targetWindow)
      return;

    if (!IsWindow(g_targetWindow))
    {
      StopDrag();
      return;
    }

    const int dx = cursor.x - g_startMouse.x;
    const int dy = cursor.y - g_startMouse.y;

    RECT rect = g_startRect;
    
    if (g_dragMode == DragMode::Move)
    {
      // Move algorithm is pretty easy.
      OffsetRect(&rect, dx, dy);
    }
    else if (g_dragMode == DragMode::Resize)
    {
      // Resize algorithm needs to enforce minimum size as well 
      // as preserve opposite edge.
      if (rect.right - rect.left >= MIN_WIDTH)
      {
        if (g_resizeFromLeft) 
          rect.left += dx;
        else 
          rect.right += dx;
      }
      
      if (rect.bottom - rect.top >= MIN_HEIGHT)
      {
        if (g_resizeFromTop) 
          rect.top += dy;
        else 
          rect.bottom += dy;
      } 
    }
    
#ifdef _DEBUG
    
    if (g_dragMode == DragMode::Move)
    {
      DebugConsole::Log(
        L"Move: (%ld,%ld)-(%ld,%ld)",
        rect.left,
        rect.top,
        rect.right,
        rect.bottom);
    }
    else
    {
      auto sideX = (g_resizeFromLeft ? L"LEFT" : L"RIGHT");
      auto sideY = (g_resizeFromTop ?  L"TOP"  : L"BOTTOM");
      
      DebugConsole::Log(
        L"Resize: x=%-4d y=%-4d (%ls %ls)", 
        dx, dy,
        sideX, sideY);
    }
    
#endif
    
    //
    // A window can refuse to change size if it wants to, so we will just ask nicely...
    //
    SetWindowPos(
      g_targetWindow,
      nullptr,
      rect.left,
      rect.top,
      rect.right - rect.left,
      rect.bottom - rect.top,
      SWP_NOZORDER | SWP_NOACTIVATE
    );
  }

  //
  // All done; stop intercepting mouse
  void StopDrag()
  {
    g_dragMode = DragMode::None;
    g_targetWindow = nullptr;
  }

  void ShowTrayMenu(HWND hwnd)
  {
    HMENU menu = CreatePopupMenu();

    if (!menu)
      return;

    AppendMenuW(
      menu,
      MF_STRING | (g_enabled ? MF_CHECKED : MF_UNCHECKED),
      ID_TRAY_TOGGLE,
      g_enabled ? L"Enabled" : L"Disabled"
    );

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"E&xit");

    POINT cursor{};
    GetCursorPos(&cursor);

    // Required for menus displayed by a notification icon.
    SetForegroundWindow(hwnd);

    TrackPopupMenu(
      menu,
      TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
      cursor.x,
      cursor.y,
      0,
      hwnd,
      nullptr
    );

    DestroyMenu(menu);

    // Helps ensure the menu closes correctly on all Windows versions.
    PostMessageW(hwnd, WM_NULL, 0, 0);
  }

  LRESULT CALLBACK MouseHookProc(
    int code,
    WPARAM wParam,
    LPARAM lParam)
  {
    if (code != HC_ACTION)
      return ::CallNextHookEx(g_mouseHook, code, wParam, lParam);

    if (!g_enabled)
      return ::CallNextHookEx(g_mouseHook, code, wParam, lParam);

    const auto* mouse =
      reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);

    // 
    // Messages are intercepted while drag occuring - otherwise you'd
    // get unwanted context menu / text selection etc.
    switch (wParam)
    {
      case WM_LBUTTONDOWN:
        if (IsShiftDown() && BeginDrag(DragMode::Move)) 
          return 1;
        break;

      case WM_RBUTTONDOWN:
        if (IsShiftDown() && BeginDrag(DragMode::Resize)) 
          return 1; 
        break;

      case WM_MOUSEMOVE:
        if (g_dragMode != DragMode::None)
        {
          // WM_MOUSEMOVE is safe to pass through to target.
          UpdateDrag(mouse->pt);
        }
        break;

      case WM_LBUTTONUP:
        if (g_dragMode == DragMode::Move) 
        {
          StopDrag();
          return 1;
        }
        break;

      case WM_RBUTTONUP:
        if (g_dragMode == DragMode::Resize) 
        {
          StopDrag();
          return 1;
        }
        break;
    }

    return ::CallNextHookEx(g_mouseHook, code, wParam, lParam);
  }

  bool AddTrayIcon(HWND hwnd)
  {
    g_trayIcon = {};
    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.hWnd = hwnd;
    g_trayIcon.uID = ID_TRAY_ICON_ID;
    g_trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIcon.uCallbackMessage = WM_TRAY_ICON;
    g_trayIcon.hIcon = g_enabled
      ? g_enabledIcon
      : g_disabledIcon;

    wcscpy_s(
      g_trayIcon.szTip,
      L"EasyWindows - Enabled"
    );

    return Shell_NotifyIconW(NIM_ADD, &g_trayIcon) != FALSE;
  }

  void UpdateTrayIcon()
  {
    wcscpy_s(
      g_trayIcon.szTip,
      g_enabled
        ? L"EasyWindows - Enabled"
        : L"EasyWindows - Disabled"
    );
    
    g_trayIcon.hIcon = g_enabled
      ? g_enabledIcon
      : g_disabledIcon;
    
    g_trayIcon.uFlags = NIF_ICON | NIF_TIP;

    Shell_NotifyIconW(NIM_MODIFY, &g_trayIcon);
  }

  void ToggleEnabled()
  {
    g_enabled = !g_enabled;

    if (!g_enabled)
      StopDrag();

    UpdateTrayIcon();
  }
  
  HICON LoadTrayIcon(int resourceId)
  {
    return static_cast<HICON>(LoadImageW(
      g_instance,
      MAKEINTRESOURCEW(resourceId),
      IMAGE_ICON,
      0,
      0,
      LR_DEFAULTSIZE
    ));
  }

  //
  // window procedure
  LRESULT CALLBACK WindowProc(HWND hwnd,
                              UINT message,
                              WPARAM wParam,
                              LPARAM lParam)
  {
    switch (message)
    {
      //
      // a Click on the notifcation icon
      case WM_TRAY_ICON:
      {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
          ShowTrayMenu(hwnd);
        else if (lParam == WM_LBUTTONUP)
          ToggleEnabled();
        
        // Skip default
        return 0;
      }

      //
      // Context menu commands
      case WM_COMMAND:
      {
        switch (LOWORD(wParam))
        {
          case ID_TRAY_TOGGLE:
            ToggleEnabled();
            return 0;

          case ID_TRAY_EXIT:
            ::DestroyWindow(hwnd);
            return 0;
        }
        break;
      }

      //
      // Close up and exit
      case WM_DESTROY:
      {
        StopDrag();

        if (g_mouseHook)
        {
          UnhookWindowsHookEx(g_mouseHook);
          g_mouseHook = nullptr;
        }

        Shell_NotifyIconW(NIM_DELETE, &g_trayIcon);
        
        if (g_enabledIcon) 
        {
          ::DestroyIcon(g_enabledIcon);
          g_enabledIcon = nullptr;
        }
        
        if (g_disabledIcon) 
        {
          ::DestroyIcon(g_disabledIcon);
          g_disabledIcon = nullptr;
        }
        
        ::PostQuitMessage(0);
        
        return 0;
      }
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
  }
}

// end of file namespace

// Entrypoint
int WINAPI wWinMain(HINSTANCE instance,
                    HINSTANCE,
                    PWSTR,
                    int)
{
  
  DebugConsole::Initialize();
  
#ifdef _DEBUG
  DebugConsole::Log(L"EasyWindows - Debug Mode");
#endif
  
  if (!SetProcessDpiAwarenessContext(
    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
  {
    SetProcessDPIAware();
  }
  
  g_instance = instance;
  
  g_enabledIcon  = LoadTrayIcon(IDI_TRAY_ENABLED);
  g_disabledIcon = LoadTrayIcon(IDI_TRAY_DISABLED);
  
  if (!g_enabledIcon || !g_disabledIcon) 
  {
    if (g_enabledIcon) 
      DestroyIcon(g_enabledIcon);
    
    if (g_disabledIcon)
      DestroyIcon(g_disabledIcon);
   
    // Fail 
    return 1;
  }

  WNDCLASSW wc{};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.lpszClassName = className;

  if (!RegisterClassW(&wc))
    return 1;

  //
  // Message window - for the menu callbacks
  g_messageWindow = CreateWindowExW(
    0,
    className,
    L"EasyWindows Message Window",
    0,
    0, 0, 0, 0,
    HWND_MESSAGE,
    nullptr,
    instance,
    nullptr
  );

  if (!g_messageWindow)
    return 1;

  if (!AddTrayIcon(g_messageWindow))
  {
    DestroyWindow(g_messageWindow);
    return 1;
  }

  // Start hook right away
  g_mouseHook = SetWindowsHookExW(
    WH_MOUSE_LL,
    MouseHookProc,
    instance,
    0
  );

  if (!g_mouseHook)
  {
    DestroyWindow(g_messageWindow);
    return 1;
  }

  MSG message{};

  while (GetMessageW(&message, nullptr, 0, 0) > 0)
  {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  return static_cast<int>(message.wParam);
}
