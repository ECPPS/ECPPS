[[dllimport]] extern "C" void ExitProcess(int);

int main()
{
     char x;
     return 0;
}

void _EntryPoint()
{
     ExitProcess(main());
}