#pragma once
#include <string>
#include <unordered_map>
#include <vector>

std::unordered_map<std::string, std::vector<std::string>> GetExportsFromDlls(const std::vector<std::string>& dlls);
