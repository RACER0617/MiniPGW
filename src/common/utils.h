#ifndef UTILS_H
#define UTILS_H
#include <cstdint>
#include <vector>
#include <string>

std::vector<uint8_t> imsiStringToBcd(const std::string& imsi);
std::string bcdToImsiString(const std::vector<uint8_t>& bcd);

#endif