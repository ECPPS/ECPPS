#ifdef __ecpps_version
#define DllImport [[dllimport]]
#else
#define DllImport
#endif

DllImport extern "C" void ExitProcess(int);

int main()
{
     ExitProcess(1 - 2);
     return 0;
}