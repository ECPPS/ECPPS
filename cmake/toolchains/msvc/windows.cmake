include(${CMAKE_CURRENT_LIST_DIR}/../base.cmake)

set(CMAKE_C_COMPILER cl)
set(CMAKE_CXX_COMPILER cl)
set(CMAKE_CXX_FLAGS_INIT /Zc:__cplusplus /Zc:enumTypes /Zc:templateScope /Zc:throwingNew /guard:cf /EHsc)
set(CMAKE_CXX_FLAGS_DEBUG_INIT /Od /Zi /sdl /GS)
set(CMAKE_CXX_FLAGS_RELEASE_INIT /O2 /Oi /fp:fast /GF /Gy /Ot /Ob3 /Gw /GL)
set(CMAKE_EXE_LINKER_FLAGS /stack:10000000)
set(CMAKE_EXE_LINKER_RELEASE_INIT /INCREMENTAL:NO /LTCG /OPT:REF /OPT:ICF)

set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
