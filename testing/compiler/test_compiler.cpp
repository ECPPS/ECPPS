#ifdef __ecpps_version
#define DllImport [[dllimport]]
#else
#define DllImport
#endif

DllImport extern "C" void ExitProcess(int);

int getValue()
{ return 5; }

int main()
{
     ExitProcess(getValue());
     return 0;
}