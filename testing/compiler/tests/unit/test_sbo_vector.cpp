#include <SBOVector.h>
#include <cassert>

int main()
{
     ecpps::SBOVector<int> vec;
     for (int i = 0; i < 16; ++i) vec.Push(i);
     if (vec.Size() != 16) return -1;
     for (int i = 0; i < 16; ++i)
     {
          if (vec.begin()[i] != i) return -1;
     }
     return 0;
}
