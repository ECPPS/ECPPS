#include "Parsing/SourceMap.h"
#include "Shared/Config.h"

int main(int argc, char* argv[])
{
     ecpps::CompilerConfig config{argc, argv};
     ecpps::SourceMap sources{config};

     return 0;
}