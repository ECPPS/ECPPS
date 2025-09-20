#pragma once
#include <unordered_map>
#include <string>
#include <vector>

std::unordered_map<std::string, std::vector<std::string>> GetExportsFromDlls(const std::vector<std::string>& dlls);