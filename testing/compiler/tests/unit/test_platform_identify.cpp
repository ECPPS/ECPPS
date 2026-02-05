#include <platformlib/identify.h>
#include <cassert>

int main()
{
     auto id = ecpps::platformlib::PlatformIdentity::Identify();
     // Just ensure it returns a valid value from the enum
     switch (id.bitness)
     {
     case ecpps::platformlib::ArchitectureBitness::x86_64:
     case ecpps::platformlib::ArchitectureBitness::x86_32:
     case ecpps::platformlib::ArchitectureBitness::ARM32:
     case ecpps::platformlib::ArchitectureBitness::ARM64: return 0;
     default: return -1;
     }
}
