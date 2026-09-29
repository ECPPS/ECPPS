using HANDLE = void*;
using DWORD = unsigned int;
using QWORD = unsigned long long;

extern "C" HANDLE GetStdHandle(int);
extern "C" int WriteConsoleA(HANDLE __hConsole, const char* __lpBuffer, DWORD __nNumberOfCharsToWrite,
                             DWORD* __lpNumberOfCharsWritten, QWORD q);

extern "C" void MessageBoxA(int, const char*, const char*, int);
extern "C" void ExitProcess(int);

int main()
{
     DWORD len = 0;
     const char* buffer = "meow";
     HANDLE stdHandle = GetStdHandle(-11);
     WriteConsoleA(stdHandle, buffer, 4, &len, 0);

     ExitProcess(len + 1);
}
