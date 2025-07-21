#include "utils.h"
#include <stdexcept>
#include <cctype>
#include "utils.h"

// Преобразование строки IMSI (15 цифр) в формат BCD (Binary-Coded Decimal)
std::vector<uint8_t> imsiStringToBcd(const std::string& imsi) {
    if (imsi.length() != 15) throw std::invalid_argument("IMSI must be 15 digits"); // Проверка длины IMSI
    std::vector<uint8_t> bcd(8, 0);
    for (size_t i = 0; i < 15; ++i) {
        char c = imsi[i];
        if (!isdigit(static_cast<unsigned char>(c))) throw std::invalid_argument("IMSI must contain only digits"); // Проверка, что символ — цифра
        int digit = c - '0';
        if (i % 2 == 0) {
            bcd[i / 2] = static_cast<uint8_t>(digit << 4);
        } else {
            bcd[i / 2] |= static_cast<uint8_t>(digit);
        }
    }
    bcd[7] |= 0x0F;
    return bcd;
}

// Преобразует 8-байтный вектор BCD обратно в строку IMSI
std::string bcdToImsiString(const std::vector<uint8_t>& bcd) {
    if (bcd.size() != 8) throw std::invalid_argument("BCD must be 8 bytes"); // Проверка размера
    std::string imsi;
    for (size_t i = 0; i < 7; ++i) {
        uint8_t high = (bcd[i] >> 4) & 0x0F;
        uint8_t low = bcd[i] & 0x0F;
        // Проверка, что полученные значения допустимы
        if (high > 9) throw std::invalid_argument("Invalid BCD digit");
        imsi += '0' + high;
        if (low > 9) throw std::invalid_argument("Invalid BCD digit");
        imsi += '0' + low;
    }
    // Обработка последнего байта: только старшие 4 бита (8-й байт хранит только одну цифру)
    uint8_t high_last = (bcd[7] >> 4) & 0x0F;
    if (high_last > 9) throw std::invalid_argument("Invalid BCD digit");
    imsi += '0' + high_last;
    return imsi;
}