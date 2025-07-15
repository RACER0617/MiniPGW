#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include "../common/config.h"
#include "../common/utils.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <config.json> <IMSI>" << std::endl;
        return 1;
    }
    std::string config_file = argv[1];
    std::string imsi_str = argv[2];
    ClientConfig config;
    try {
        config = loadClientConfig(config_file);
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return 1;
    }
    auto logger = spdlog::basic_logger_mt("client_logger", config.log_file);
    spdlog::level::level_enum log_level = spdlog::level::from_str(config.log_level);
    logger->set_level(log_level);
    logger->info("Client started with IMSI: {}", imsi_str);
    std::vector<uint8_t> bcd;
    try {
        bcd = imsiStringToBcd(imsi_str);
    } catch (const std::exception& e) {
        logger->error("Invalid IMSI: {}", e.what());
        return 1;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        logger->error("Cannot create UDP socket");
        return 1;
    }
    sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(config.server_port);
    if (inet_pton(AF_INET, config.server_ip.c_str(), &serv_addr.sin_addr) <= 0) {
        logger->error("Invalid server IP address");
        close(sock);
        return 1;
    }
    if (sendto(sock, static_cast<const void*>(bcd.data()), bcd.size(), 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        logger->error("Sendto failed");
        close(sock);
        return 1;
    }
    char buffer[10];
    socklen_t len = sizeof(serv_addr);
    int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&serv_addr, &len);
    if (n < 0) {
        logger->error("Recvfrom failed");
        close(sock);
        return 1;
    }
    buffer[n] = '\0';
    std::string response(buffer);
    logger->info("Received response: {}", response);
    std::cout << response << std::endl;
    close(sock);
    return 0;
}