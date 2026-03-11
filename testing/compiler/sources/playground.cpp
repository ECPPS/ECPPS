// NOLINTBEGIN

using _INT8 = signed char;
using _UINT8 = unsigned char;
using _INT16 = short;
using _UINT16 = unsigned short;
using _INT32 = int;
using _UINT32 = unsigned int;
using _INT64 = long long;
using _UINT64 = unsigned long long;
using _BYTE = unsigned char;
using _WORD = unsigned short;
using _DWORD = unsigned long;
using _QWORD = unsigned long long;
using _HANDLE = void*;
using _BOOL = int;
using _CHAR = char;

#ifdef __ecpps_version
#define _DLLIMPORT(name) [[dllimport(#name)]]
#elifdef _MSC_VER
#define _AS(__n) __pragma(comment(linker, "/alternatename:__" #__n "=" #__n))

#define _DLLIMPORT(__n) extern "C" _AS(__n)

#else
#error Invalid implementation
#endif

_DLLIMPORT(ExitProcess) void __ExitProcess(_UINT32);
_DLLIMPORT(AttachConsole) _BOOL __AttachConsole(_DWORD);
_DLLIMPORT(AllocConsole) _BOOL __AllocConsole();
_DLLIMPORT(GetLastError) _DWORD __GetLastError();
_DLLIMPORT(lstrlenA) int __lstrlenA(const char*);
_DLLIMPORT(DebugBreak) void __DebugBreak();
_DLLIMPORT(GetStdHandle) _HANDLE __GetStdHandle(_DWORD);
_DLLIMPORT(WriteFile) _BOOL __WriteFile(_HANDLE, const void*, _DWORD, _DWORD*, _QWORD);
_DLLIMPORT(WriteConsoleA)
_BOOL __WriteConsoleA(_HANDLE __hConsole, const char* __lpBuffer, _DWORD __nNumberOfCharsToWrite,
                      _DWORD* __lpNumberOfCharsWritten, _QWORD q);

_HANDLE __std_stdin_handle() { return __GetStdHandle(0 - 10); }
_HANDLE __std_stdout_handle() { return __GetStdHandle(0 - 11); }
_HANDLE __std_stderr_handle() { return __GetStdHandle(0 - 12); }
_DWORD __std_console_write(_HANDLE __hConsole, const char* __buffer, _DWORD __length)
{
     _DWORD __numberOfCharsWritten;
     __WriteConsoleA(__hConsole, __buffer, __length, &__numberOfCharsWritten, 0);
     return __numberOfCharsWritten;
}

int __std_strlen(const char* __string) { return __lstrlenA(__string); }

int __std_cwrite(const char* __buffer)
{
     _HANDLE __hConsole = __std_stdout_handle();
     _DWORD __length = __std_strlen(__buffer);
     return __std_console_write(__hConsole, __buffer, __length);
}

int main() { return __std_cwrite("purristic!"); }

#ifdef __ecpps_version
extern "C" void _EntryPoint() { __ExitProcess(main()); }
#endif

// NOLINTEND
