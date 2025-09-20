[[dllimport]] extern "C" void ExitProcess(int code);
[[dllimport]] extern "C" void VirtualAlloc(int code);

int Meow(int s)
{
	return 4;
}

int main()
{
	return Meow(123);
}