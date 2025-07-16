#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <set>
#include <chrono>
#include <fstream>
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "../common/config.h"
#include "../common/utils.h"
#include <iomanip>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

struct Session {
    std::chrono::steady_clock::time_point creation_time;
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config.json>" << std::endl;
        return 1;
    }
    std::string config_file = argv[1];
    ServerConfig config;
    try {
        config = loadServerConfig(config_file);
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return 1;
    }

    // Initialize logger with console and file sinks
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.log_file, true);
    file_sink->set_level(spdlog::level::info);
    auto logger = std::make_shared<spdlog::logger>("pgw_logger",
        spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::register_logger(logger);

    logger->info("Server logging initialized: UDP {}:{} HTTP port {} CDR file {}",
        config.udp_ip, config.udp_port, config.http_port, config.cdr_file);

    std::ofstream cdr_stream(config.cdr_file, std::ios::app);
    if (!cdr_stream.is_open()) {
        logger->error("Cannot open CDR file: {}", config.cdr_file);
        spdlog::shutdown();
        return 1;
    }

    std::map<std::string, Session> sessions;
    std::set<std::string> blacklist(config.blacklist.begin(), config.blacklist.end());
    bool shutting_down = false;
    bool shutdown_complete = false;
    std::mutex mutex;
    std::condition_variable cv;
    std::mutex cdr_mutex;

    auto udp_function = [&]() {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            logger->error("Cannot create UDP socket: {}", strerror(errno));
            return;
        }
        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = inet_addr(config.udp_ip.c_str());
        serv_addr.sin_port = htons(config.udp_port);
        if (bind(sock, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
            logger->error("Bind failed: {}", strerror(errno));
            close(sock);
            return;
        }
        logger->info("UDP server listening on {}:{}", config.udp_ip, config.udp_port);
        while (true) {
            { std::lock_guard<std::mutex> lock(mutex);
              if (shutting_down) break; }
            char buffer[8];
            sockaddr_in cli_addr;
            socklen_t len = sizeof(cli_addr);
            int n = recvfrom(sock, buffer, sizeof(buffer), 0,
                             reinterpret_cast<struct sockaddr*>(&cli_addr), &len);
            if (n < 0) {
                logger->error("Recvfrom failed: {}", strerror(errno));
                continue;
            }
            if (n != 8) {
                logger->warn("Received packet of size {} != 8", n);
                continue;
            }
            std::vector<uint8_t> bcd(buffer, buffer + 8);
            std::string imsi;
            try { imsi = bcdToImsiString(bcd); }
            catch (const std::exception& e) {
                logger->warn("Invalid BCD: {}", e.what());
                continue;
            }
            { std::lock_guard<std::mutex> lock(mutex);
              if (blacklist.count(imsi) || sessions.count(imsi)) {
                  sendto(sock, "rejected", 8, 0,
                         reinterpret_cast<struct sockaddr*>(&cli_addr), len);
                  logger->info("Subscriber {} rejected", imsi);
              } else {
                  sessions[imsi] = Session{std::chrono::steady_clock::now()};
                  { std::lock_guard<std::mutex> cdr_lock(cdr_mutex);
                    auto now_c = std::time(nullptr);
                    cdr_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S")
                               << "," << imsi << ",create" << std::endl;
                    cdr_stream.flush(); }
                  logger->info("Session created for IMSI {}", imsi);
                  sendto(sock, "created", 7, 0,
                         reinterpret_cast<struct sockaddr*>(&cli_addr), len);
            } }
        }
        close(sock);
    };

    auto http_function = [&]() {
        httplib::Server svr;
        svr.Get("/check_subscriber", [&](const httplib::Request& req, httplib::Response& res) {
            auto it = req.params.find("imsi");
            if (it == req.params.end()) {
                res.status = 400; res.body = "Missing imsi param"; return; }
            std::string imsi = it->second;
            std::lock_guard<std::mutex> lock(mutex);
            res.body = sessions.count(imsi) ? "active" : "not active";
        });
        svr.Get("/stop", [&](const httplib::Request&, httplib::Response& res) {
            { std::lock_guard<std::mutex> lock(mutex); shutting_down = true; }
            svr.stop(); res.status = 200; res.body = "Shutdown initiated";
        });
        svr.listen("0.0.0.0", config.http_port);
    };

    auto cleanup_function = [&]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::lock_guard<std::mutex> lock(mutex);
            if (shutting_down && sessions.empty()) { shutdown_complete = true; cv.notify_one(); break; }
            std::vector<std::string> to_delete;
            auto now_point = std::chrono::steady_clock::now();
            for (auto& [imsi, sess] : sessions) {
                if (now_point - sess.creation_time > std::chrono::seconds(config.session_timeout_sec)) {
                    to_delete.push_back(imsi);
                }
            }
            for (auto& imsi : to_delete) {
                { std::lock_guard<std::mutex> cdr_lock(cdr_mutex);
                  auto now_c = std::time(nullptr);
                  cdr_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S")
                             << "," << imsi << ",delete" << std::endl;
                  cdr_stream.flush(); }
                sessions.erase(imsi);
                logger->info("Session deleted for IMSI {}", imsi);
            }
        }
    };

    std::thread t1(udp_function), t2(http_function), t3(cleanup_function);
    { std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&]{ return shutdown_complete; }); }
    t1.join(); t2.join(); t3.join();

    cdr_stream.close();
    spdlog::shutdown();
    return 0;
}