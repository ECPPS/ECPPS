#ifdef __ecpps_version
#define DllImport [[dllimport]]
#else
#define DllImport
#endif

DllImport extern "C" void ExitProcess(int);

int main()
{
     ExitProcess(21);
     return 0;
}