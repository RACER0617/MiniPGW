#ifndef CONFIG_H
#define CONFIG_H

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct ServerConfig {
    std::string udp_ip;
    int udp_port;
    int session_timeout_sec;
    std::string cdr_file;
    int http_port;
    int graceful_shutdown_rate;
    std::string log_file;
    std::string log_level;
    std::vector<std::string> blacklist;
};

struct ClientConfig {
    std::string server_ip;
    int server_port;
    std::string log_file;
    std::string log_level;
};

ServerConfig loadServerConfig(const std::string& filename);
ClientConfig loadClientConfig(const std::string& filename);

#endif