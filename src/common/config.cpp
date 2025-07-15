#include "config.h"
#include <fstream>
#include <stdexcept>

ServerConfig loadServerConfig(const std::string& filename) {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filename);
    }
    nlohmann::json j;
    ifs >> j;
    ServerConfig config;
    config.udp_ip = j["udp_ip"];
    config.udp_port = j["udp_port"];
    config.session_timeout_sec = j["session_timeout_sec"];
    config.cdr_file = j["cdr_file"];
    config.http_port = j["http_port"];
    config.graceful_shutdown_rate = j["graceful_shutdown_rate"];
    config.log_file = j["log_file"];
    config.log_level = j["log_level"];
    for (const auto& bl : j["blacklist"]) {
        config.blacklist.push_back(bl.get<std::string>());
    }
    return config;
}

ClientConfig loadClientConfig(const std::string& filename) {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filename);
    }
    nlohmann::json j;
    ifs >> j;
    ClientConfig config;
    config.server_ip = j["server_ip"];
    config.server_port = j["server_port"];
    config.log_file = j["log_file"];
    config.log_level = j["log_level"];
    return config;
}