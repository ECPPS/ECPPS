#include "BumpAllocator.h"

#include <algorithm>
#include <format>

#include <RuntimeAssert.h>
#include "SBOVector.h"

#ifdef _WIN32
#include <Windows.h>

static void* ReserveMemory(std::size_t count) noexcept
{
     return VirtualAlloc(nullptr, count, MEM_RESERVE, PAGE_NOACCESS);
}

static void CommitMemory(void* address, std::size_t count) noexcept
{
     [[maybe_unused]] auto* const committed = VirtualAlloc(address, count, MEM_COMMIT, PAGE_READWRITE);
     runtime_assert(committed == address, std::format("Commit failed: {}", GetLastError()));
}

static void ReleaseMemory(void* address, [[maybe_unused]] std::size_t count) noexcept
{
     VirtualFree(address, 0, MEM_RELEASE);
}
#elifdef __linux__
#include <sys/mman.h>

static void* ReserveMemory(std::size_t count) noexcept
{
     void* result = mmap(nullptr, count, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
     if (result == MAP_FAILED) return nullptr;
     return result;
}

static void CommitMemory(void* address, std::size_t count) noexcept
{
     if (mprotect(address, count, PROT_READ | PROT_WRITE) != 0)
     {
          runtime_assert(false, std::format("Commit failed: {}", errno));
     }
}

static void ReleaseMemory(void* address, std::size_t count) noexcept
{
     if (munmap(address, count) != 0) { runtime_assert(false, std::format("Release failed: {}", errno)); }
}
#endif

constexpr std::size_t PageSize = 4096;
constexpr std::size_t CommitStep = 16uz * PageSize; // 64kiB

ecpps::BumpAllocator::BumpAllocator(std::size_t maxMemory)
{
     if (maxMemory == 0) maxMemory = 2uz * 1024uz * 1024uz * 1024uz * 1024uz; // 2TiB
     maxMemory = std::max(maxMemory, CommitStep);

     this->_begin = static_cast<std::byte*>(ReserveMemory(maxMemory));
     this->_capacity = this->_begin + maxMemory;
     this->_currentEnd = this->_begin;
     this->_commitEnd = this->_begin;
}
std::byte* ecpps::BumpAllocator::Allocate(std::size_t size) noexcept
{
     size = Align(size, sizeof(std::max_align_t));

     auto* address = std::exchange(this->_currentEnd, this->_currentEnd + size);
     if (this->_currentEnd >= this->_capacity) return nullptr;

     while (this->_currentEnd >= this->_commitEnd)
     {
          CommitMemory(this->_commitEnd, CommitStep);
          this->_commitEnd += CommitStep;
     }
     return address;
}

ecpps::BumpAllocator::~BumpAllocator(void) { Release(); }

void ecpps::BumpAllocator::Release(void)
{
     ReleaseMemory(std::exchange(this->_begin, nullptr), static_cast<std::size_t>(this->_capacity - this->_begin));
}
