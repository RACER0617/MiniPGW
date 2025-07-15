#include <gtest/gtest.h>
#include "../src/common/utils.h"
#include <stdexcept>

TEST(UtilsTest, ImsiStringToBcd_Valid15Digits) {
    std::string imsi = "123456789012345";
    std::vector<uint8_t> expected = {0x12, 0x34, 0x56, 0x78, 0x90, 0x12, 0x34, 0x5F};
    std::vector<uint8_t> result = imsiStringToBcd(imsi);
    EXPECT_EQ(result, expected);
}

TEST(UtilsTest, ImsiStringToBcd_InvalidLength) {
    std::string imsi_short = "12345678901234"; // 14 цифр
    std::string imsi_long = "1234567890123456"; // 16 цифр
    EXPECT_THROW(imsiStringToBcd(imsi_short), std::invalid_argument);
    EXPECT_THROW(imsiStringToBcd(imsi_long), std::invalid_argument);
}

TEST(UtilsTest, ImsiStringToBcd_NonDigit) {
    std::string imsi_invalid = "12345678901234a";
    EXPECT_THROW(imsiStringToBcd(imsi_invalid), std::invalid_argument);
}

TEST(UtilsTest, BcdToImsiString_ValidBcd) {
    std::vector<uint8_t> bcd = {0x12, 0x34, 0x56, 0x78, 0x90, 0x12, 0x34, 0x5F};
    std::string expected = "123456789012345";
    std::string result = bcdToImsiString(bcd);
    EXPECT_EQ(result, expected);
}

TEST(UtilsTest, BcdToImsiString_InvalidBcdSize) {
    std::vector<uint8_t> bcd_short = {0x12, 0x34, 0x56, 0x78};
    std::vector<uint8_t> bcd_long = {0x12, 0x34, 0x56, 0x78, 0x90, 0x12, 0x34, 0x5F, 0x00};
    EXPECT_THROW(bcdToImsiString(bcd_short), std::invalid_argument);
    EXPECT_THROW(bcdToImsiString(bcd_long), std::invalid_argument);
}

TEST(UtilsTest, BcdToImsiString_InvalidDigit) {
    std::vector<uint8_t> bcd_invalid = {0x1F, 0x34, 0x56, 0x78, 0x90, 0x12, 0x34, 0x5F};
    EXPECT_THROW(bcdToImsiString(bcd_invalid), std::invalid_argument);
}

TEST(UtilsTest, RoundTripConversion) {
    std::string imsi = "001010123456789";
    std::vector<uint8_t> bcd = imsiStringToBcd(imsi);
    std::string result = bcdToImsiString(bcd);
    EXPECT_EQ(result, imsi);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}