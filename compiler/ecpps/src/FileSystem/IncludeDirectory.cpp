#include "IncludeDirectory.h"

#include <limits>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#elifdef __linux__
#include <unistd.h>
#include <cerrno>
#elifdef __APPLE__
#include <cstdint>
#include <mach-o/dyld.h>
#else
#error "Executable path lookup is not implemented for this platform"
#endif

namespace
{
#ifdef _WIN32
     [[nodiscard]] std::string SystemErrorMessage(DWORD code)
     {
          return std::error_code(static_cast<int>(code), std::system_category()).message();
     }
#elifdef __linux__
     [[nodiscard]] std::string SystemErrorMessage(int code)
     {
          return std::error_code(code, std::generic_category()).message();
     }
#endif
} // namespace

ecpps::fs::PathResult ecpps::fs::NativeExecutablePathProvider::GetExecutablePath(void) const
{
#ifdef _WIN32
     std::wstring buffer(32768, L'\0');
     const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
     if (length == 0) return {.error = "GetModuleFileNameW failed: " + SystemErrorMessage(GetLastError())};
     if (length >= buffer.size()) return {.error = "executable path is too long"};
     return {.path = std::filesystem::path(buffer.substr(0, length))};
#elifdef __linux__
     std::vector<char> buffer(256);
     while (true)
     {
          const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size());
          if (length < 0) return {.error = "readlink(/proc/self/exe) failed: " + SystemErrorMessage(errno)};
          if (static_cast<std::size_t>(length) < buffer.size())
               return {.path = std::string(buffer.data(), static_cast<std::size_t>(length))};
          if (buffer.size() > std::numeric_limits<std::size_t>::max() / 2)
               return {.error = "executable path is too long"};
          buffer.resize(buffer.size() * 2);
     }
#elifdef __APPLE__
     std::uint32_t size{};
     [[maybe_unused]] const auto probeResult = _NSGetExecutablePath(nullptr, &size);
     if (size == 0) return {.error = "_NSGetExecutablePath did not provide a buffer size"};

     std::vector<char> buffer(size);
     if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {.error = "_NSGetExecutablePath failed"};
     return {.path = buffer.data()};
#endif
}

ecpps::fs::PathResult ecpps::fs::NormalisePath(const std::filesystem::path& path)
{
     if (path.empty()) return {.error = "path is empty"};

     std::error_code error;
     auto normalised = std::filesystem::weakly_canonical(path, error);
     if (error) return {.error = "path canonicalisation failed: " + error.message()};
     if (!normalised.is_absolute()) return {.error = "path is not absolute"};

     normalised = normalised.lexically_normal();
     while (normalised != normalised.root_path() && normalised.filename().empty())
          normalised = normalised.parent_path();
     return {.path = std::move(normalised)};
}

std::string ecpps::fs::ValidateDirectory(const std::filesystem::path& path)
{
     if (path.empty()) return "invalid directory path: path is empty";

     std::error_code error;
     if (!std::filesystem::exists(path, error))
          return error ? "invalid directory path: " + error.message() : "directory is missing: " + path.string();
     if (!std::filesystem::is_directory(path, error))
          return error ? "invalid directory path: " + error.message() : "path is not a directory: " + path.string();
     return {};
}

ecpps::fs::PathResult ecpps::fs::DetectIncludeDirectory(const IExecutablePathProvider& provider)
{
     const auto executable = provider.GetExecutablePath();
     if (!executable)
     {
          if (executable.error.empty()) return {.error = "invalid executable path: path is empty"};
          return {.error = "executable path lookup failed: " + executable.error};
     }

     const auto normalisedExecutable = NormalisePath(executable.path);
     if (!normalisedExecutable) return {.error = "invalid executable path: " + normalisedExecutable.error};

     std::error_code error;
     const auto isRegularFile = std::filesystem::is_regular_file(normalisedExecutable.path, error);
     if (error || !isRegularFile) return {.error = "invalid executable path: " + normalisedExecutable.path.string()};

     const auto candidate = normalisedExecutable.path.parent_path() / ".." / "include";
     auto normalisedInclude = NormalisePath(candidate);
     if (!normalisedInclude) return {.error = "invalid include path: " + normalisedInclude.error};

     const auto validationError = ValidateDirectory(normalisedInclude.path);
     if (!validationError.empty()) return {.error = validationError};
     return normalisedInclude;
}

ecpps::fs::PathResult ecpps::fs::DetectIncludeDirectory(void)
{
     return DetectIncludeDirectory(NativeExecutablePathProvider{});
}
