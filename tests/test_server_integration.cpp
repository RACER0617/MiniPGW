#include <gtest/gtest.h>
#include "../common/utils.h"

TEST(BcdToImsiStringTest, ValidBcd) {
    // Пример BCD: IMSI "130151024365879"
    std::vector<uint8_t> bcd = {0x13, 0x01, 0x51, 0x02, 0x43, 0x65, 0x87, 0x9F};
    std::string imsi = bcdToImsiString(bcd);
    EXPECT_EQ(imsi, "130151024365879");
}

TEST(BcdToImsiStringTest, InvalidLength) {
    std::vector<uint8_t> bcd = {0x01, 0x23};  // слишком короткий
    EXPECT_THROW(bcdToImsiString(bcd), std::exception);
}

TEST(BcdToImsiStringTest, InvalidNibble) {
    // 0x1A – A > 9, некорректная BCD цифра
    std::vector<uint8_t> bcd = {0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    EXPECT_THROW(bcdToImsiString(bcd), std::exception);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
