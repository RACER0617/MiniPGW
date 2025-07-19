#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "../common/config.h"
#include "../common/utils.h"

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <config.json> <IMSI> [debug]" << std::endl;
        return 1;
    }
    std::string config_file = argv[1];
    std::string imsi_str    = argv[2];
    bool enable_debug       = (argc == 4 && std::string(argv[3]) == "debug");

    // Загрузка конфига
    ClientConfig config;
    try {
        config = loadClientConfig(config_file);
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return 1;
    }

    // Настройка логгера
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    // В консоль: debug только если enable_debug, иначе info+
    console_sink->set_level(enable_debug ? spdlog::level::debug : spdlog::level::info);
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.log_file, true);
    // В файл: всегда логируется всё
    file_sink->set_level(spdlog::level::debug);

    auto logger = std::make_shared<spdlog::logger>(
        "client_logger",
        spdlog::sinks_init_list{console_sink, file_sink}
    );

    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);

    logger->info("Client starting, IMSI={}, config={}, debug={}",
                 imsi_str, config_file, enable_debug);
    logger->debug("Loaded client config: server_ip={}, server_port={}, log_file={}",
                  config.server_ip, config.server_port, config.log_file);

    // Подготовка BCD-IMSI
    std::vector<uint8_t> bcd;
    try {
        bcd = imsiStringToBcd(imsi_str);
    } catch (const std::exception& e) {
        logger->error("Invalid IMSI '{}': {}", imsi_str, e.what());
        spdlog::shutdown();
        return 1;
    }
    logger->debug("IMSI '{}' -> BCD bytes: [{}]", imsi_str,
                  fmt::join(bcd, ","));

    // Открытие UDP-сокета
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        logger->critical("Cannot create UDP socket: {}", strerror(errno));
        spdlog::shutdown();
        return 1;
    }
    logger->debug("UDP socket created (fd={})", sock);

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(config.server_port);
    if (inet_pton(AF_INET, config.server_ip.c_str(), &serv_addr.sin_addr) <= 0) {
        logger->critical("Invalid server IP address: {}", config.server_ip);
        close(sock);
        spdlog::shutdown();
        return 1;
    }
    logger->debug("Server address configured: {}:{}", config.server_ip, config.server_port);

    // Отправка запроса
    ssize_t sent = sendto(sock,
                          bcd.data(),
                          bcd.size(),
                          0,
                          reinterpret_cast<struct sockaddr*>(&serv_addr),
                          sizeof(serv_addr));
    if (sent < 0) {
        logger->error("sendto failed: {}", strerror(errno));
        close(sock);
        spdlog::shutdown();
        return 1;
    }
    logger->info("Sent {} bytes to {}:{}", sent,
                 config.server_ip, config.server_port);

    // Приём ответа
    char buffer[64] = {};
    socklen_t len = sizeof(serv_addr);
    ssize_t n = recvfrom(sock,
                         buffer,
                         sizeof(buffer) - 1,
                         0,
                         reinterpret_cast<struct sockaddr*>(&serv_addr),
                         &len);
    if (n < 0) {
        logger->error("recvfrom failed: {}", strerror(errno));
        close(sock);
        spdlog::shutdown();
        return 1;
    }
    buffer[n] = '\0';
    std::string response(buffer);
    logger->info("Received response ({} bytes): '{}'", n, response);

    std::cout << response << std::endl;
    close(sock);
    spdlog::shutdown();
    return 0;
}
