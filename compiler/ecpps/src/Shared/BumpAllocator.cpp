#include "BumpAllocator.h"

#include "SBOVector.h"

#ifdef _WIN32
#include <Windows.h>

static void* ReserveMemory(std::size_t count) noexcept
{
     return VirtualAlloc(nullptr, count, MEM_RESERVE, PAGE_NOACCESS);
}

static void CommitMemory(void* address, std::size_t count) noexcept
{
     VirtualAlloc(address, count, MEM_COMMIT, PAGE_READWRITE);
}

static void ReleaseMemory(void* address) noexcept { VirtualFree(address, 0, MEM_RELEASE); }
#elifdef __linux__
#endif

constexpr std::size_t PageSize = 4096;
constexpr std::size_t CommitStep = 16uz * PageSize; // 64kiB

ecpps::BumpAllocator::BumpAllocator(std::size_t maxMemory)
{
     if (maxMemory == 0) maxMemory = 2uz * 1024uz * 1024uz * 1024uz * 1024uz; // 2TiB
     this->_begin = static_cast<std::byte*>(ReserveMemory(maxMemory));
     this->_capacity = this->_begin + maxMemory;
     this->_currentEnd = this->_begin;
     this->_commitEnd = this->_begin;
}
std::byte* ecpps::BumpAllocator::Allocate(std::size_t size) noexcept
{
     size = Align(size, sizeof(std::max_align_t));

     auto* address = std::exchange(this->_currentEnd, this->_currentEnd + size);
     while (this->_currentEnd > this->_commitEnd)
     {
          CommitMemory(this->_commitEnd, CommitStep);
          this->_commitEnd += CommitStep;
     }
     return address;
}

ecpps::BumpAllocator::~BumpAllocator(void) { Release(); }

void ecpps::BumpAllocator::Release(void) { ReleaseMemory(std::exchange(this->_begin, nullptr)); }
