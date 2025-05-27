unsigned long long operator""s(unsigned long long argument) { return argument; }
unsigned long long operator""_udl(unsigned long long argument) { return argument; }

int main() { return 0; }

long f(int arg) { return arg + 5ll + 2s * 120_udl; }