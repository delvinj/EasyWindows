#include "DebugConsole.h"

//
// Simple console debugging for simple win32 apps
//
// Usage: 
//     #define _DEBUG
//     DebugConsole::Initialize();
//     DebugConsole::Log(L"Hello");
// Example with formatted values:
// DebugConsole::Log(
//     L"Drag started: mode=%s hwnd=%p bounds=(%ld,%ld)-(%ld,%ld)",
//     mode == DragMode::Move ? L"Move" : L"Resize",
//     static_cast<void*>(g_targetWindow),
//     g_startRect.left,
//     g_startRect.top,
//     g_startRect.right,
//     g_startRect.bottom);
//

#ifdef _DEBUG

#include <windows.h>
#include <cstdio>
#include <cstdarg>

namespace DebugConsole
{
  
  void Initialize()
  {
    if (!::AttachConsole(ATTACH_PARENT_PROCESS)) 
      ::AllocConsole();
    
    FILE* stream = nullptr;
    
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    
    ::SetConsoleTitleW(L"DebugConsole");
  }
  
  void Log(const wchar_t* format, ...)
  {
    SYSTEMTIME time{};
    ::GetLocalTime(&time);
    
    wprintf(
      L"[%02u:%02u:%02u.%03u] ",
      time.wHour,
      time.wMinute,
      time.wSecond,
      time.wMilliseconds );
    
    va_list args;
    va_start(args, format);
    vwprintf(format, args);
    va_end(args);
    
    wprintf(L"\n");
    
    fflush(stdout);
  }
}

#else

namespace DebugConsole
{
 void Initialize() {}
 void Log(const wchar_t*, ...) {}
}

#endif