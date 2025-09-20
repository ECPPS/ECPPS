#include "dllHelp.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#pragma comment(lib, "dbghelp.lib")

std::unordered_map<std::string, std::vector<std::string>> GetExportsFromDlls(const std::vector<std::string>& dlls)
{
     std::unordered_map<std::string, std::vector<std::string>> result;

     for (const auto& dllName : dlls)
     {
          HMODULE module = LoadLibraryExA(dllName.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
          if (!module)
               continue;

          ULONG size = 0;
          const auto* exports = static_cast<PIMAGE_EXPORT_DIRECTORY>(
               ImageDirectoryEntryToData(module, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &size));

          if (!exports || exports->NumberOfNames == 0)
          {
               FreeLibrary(module);
               continue;
          }

          auto* names = reinterpret_cast<DWORD*>(
               reinterpret_cast<std::byte*>(module) + exports->AddressOfNames);

          for (DWORD i = 0; i < exports->NumberOfNames; ++i)
          {
               const char* funcName = reinterpret_cast<const char*>(reinterpret_cast<std::byte*>(module) + names[i]);

               result[dllName].emplace_back(funcName);
          }

          FreeLibrary(module);
     }

     return result;
}
