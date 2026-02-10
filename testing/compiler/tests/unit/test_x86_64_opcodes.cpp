#include <CodeGeneration/Emitters/x86_64/Opcodes.h>
#include <cassert>
#include <print>
#include <ranges>

bool AreEqual(const std::vector<std::byte>& a, const std::vector<std::byte>& b) noexcept
{
     if (a.size() != b.size()) return false;

     return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

constexpr std::byte operator""_b(const unsigned long long value) noexcept
{
     return static_cast<std::byte>(value & 0xff);
}

int main()
{
     bool isOkay = true;

     const auto compare =
         [&isOkay](const std::vector<std::byte>& a, const std::vector<std::byte>& b, std::string_view name)
     {
          if (AreEqual(a, b)) return;

          isOkay = false;
          std::println("[{}] Failed: {} != {}", name,
                       a | std::views::transform([](const std::byte byte) { return std::to_underlying(byte); }),
                       b | std::views::transform([](const std::byte byte) { return std::to_underlying(byte); }));
     };

     namespace x8664 = ecpps::codegen::x86_64;

     compare(x8664::GenerateSignedMulMemToReg32(0, 0, 0), std::vector<std::byte>{0x0f_b, 0xaf_b, 0x08_b},
             "imul.32 reg, mem");

     return isOkay ? 0 : -1;
}
