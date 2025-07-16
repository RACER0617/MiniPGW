#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
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

    // Initialize logger with console and file sinks
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.log_file, true);
    file_sink->set_level(spdlog::level::info);
    auto logger = std::make_shared<spdlog::logger>("client_logger",
        spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::register_logger(logger);

    logger->info("Client logging initialized: file={} level={} ", config.log_file, config.log_level);
    logger->info("Client started with IMSI: {}", imsi_str);

    std::vector<uint8_t> bcd;
    try {
        bcd = imsiStringToBcd(imsi_str);
    } catch (const std::exception& e) {
        logger->error("Invalid IMSI: {}", e.what());
        spdlog::shutdown();
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        logger->error("Cannot create UDP socket: {}", strerror(errno));
        spdlog::shutdown();
        return 1;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(config.server_port);
    if (inet_pton(AF_INET, config.server_ip.c_str(), &serv_addr.sin_addr) <= 0) {
        logger->error("Invalid server IP address: {}", config.server_ip);
        close(sock);
        spdlog::shutdown();
        return 1;
    }

    if (sendto(sock, bcd.data(), bcd.size(), 0,
               reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        logger->error("Sendto failed: {}", strerror(errno));
        close(sock);
        spdlog::shutdown();
        return 1;
    }
    logger->info("Sent IMSI request to {}:{}", config.server_ip, config.server_port);

    char buffer[16]{};
    socklen_t len = sizeof(serv_addr);
    int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                     reinterpret_cast<struct sockaddr*>(&serv_addr), &len);
    if (n < 0) {
        logger->error("Recvfrom failed: {}", strerror(errno));
        close(sock);
        spdlog::shutdown();
        return 1;
    }
    buffer[n] = '\0';
    std::string response(buffer);
    logger->info("Received response: {}", response);
    std::cout << response << std::endl;

    close(sock);
    spdlog::shutdown();
    return 0;
}