#include "Shared/Config.h"
#include "Parsing\SourceMap.h"

int main(int argc, char* argv[])
{
     ecpps::CompilerConfig config{argc, argv};
     ecpps::SourceMap sources{ config };

     return 0;
}