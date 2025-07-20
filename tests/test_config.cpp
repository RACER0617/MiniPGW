#include <gtest/gtest.h>
#include "../src/common/config.h"
#include <fstream>
#include <cstdio>
#include <stdexcept>

static void writeFile(const std::string& name, const std::string& content) {
    std::ofstream ofs(name);
    ASSERT_TRUE(ofs.is_open());
    ofs << content;
    ofs.close();
}

TEST(ConfigTest, LoadServerConfigValid) {
    const std::string fname = "test_server_config.json";
    writeFile(fname, R"({
        "udp_ip":"1.2.3.4",
        "udp_port":1234,
        "session_timeout_sec":5,
        "cdr_file":"cdr.log",
        "http_port":5678,
        "graceful_shutdown_rate":2,
        "log_file":"log.log",
        "log_level":"DEBUG",
        "blacklist":["111","222"]
    })");

    ServerConfig cfg = loadServerConfig(fname);
    EXPECT_EQ(cfg.udp_ip, "1.2.3.4");
    EXPECT_EQ(cfg.udp_port, 1234);
    EXPECT_EQ(cfg.session_timeout_sec, 5);
    EXPECT_EQ(cfg.cdr_file, "cdr.log");
    EXPECT_EQ(cfg.http_port, 5678);
    EXPECT_EQ(cfg.graceful_shutdown_rate, 2);
    EXPECT_EQ(cfg.log_file, "log.log");
    EXPECT_EQ(cfg.log_level, "DEBUG");
    EXPECT_EQ(cfg.blacklist.size(), 2u);
    EXPECT_EQ(cfg.blacklist[0], "111");
    EXPECT_EQ(cfg.blacklist[1], "222");
    std::remove(fname.c_str());
}

TEST(ConfigTest, LoadServerConfigMissingFile) {
    EXPECT_THROW(loadServerConfig("no_such_file.json"), std::runtime_error);
}

TEST(ConfigTest, LoadClientConfigValid) {
    const std::string fname = "test_client_config.json";
    writeFile(fname, R"({
        "server_ip":"9.8.7.6",
        "server_port":4321,
        "log_file":"client.log",
        "log_level":"INFO"
    })");

    ClientConfig cfg = loadClientConfig(fname);
    EXPECT_EQ(cfg.server_ip, "9.8.7.6");
    EXPECT_EQ(cfg.server_port, 4321);
    EXPECT_EQ(cfg.log_file, "client.log");
    EXPECT_EQ(cfg.log_level, "INFO");
    std::remove(fname.c_str());
}

TEST(ConfigTest, LoadClientConfigMissingFile) {
    EXPECT_THROW(loadClientConfig("no_client.json"), std::runtime_error);
}
