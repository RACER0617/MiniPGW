#include <gtest/gtest.h>
#include <cstdlib>
#include <sys/socket.h>
#include <thread>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <cstdio>

// Тест отправки и приёма
TEST(ClientIntegration, SuccessfulRequest) {
    // 1) Запуск заглушки UDP-сервера, которая сразу отвечает "rejected"
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sock, 0);
    sockaddr_in serv{};
    serv.sin_family = AF_INET;
    serv.sin_port   = htons(31000);
    inet_pton(AF_INET, "127.0.0.1", &serv.sin_addr);
    bind(sock, (sockaddr*)&serv, sizeof(serv));

    // чтение в фоне 8 байт и перессылка обратно "rejected"
    std::thread srv([&](){
        char buf[8];
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        recvfrom(sock, buf, 8, 0, (sockaddr*)&cli, &len);
        sendto(sock, "rejected", 8, 0, (sockaddr*)&cli, len);
        close(sock);
    });

    // 2) создание конфига клиента
    const char* cfg = R"({
        "server_ip":"127.0.0.1",
        "server_port":31000,
        "log_file":"cli.log",
        "log_level":"DEBUG"
    })";
    std::ofstream ofs("cfg_cli.json"); ofs<<cfg; ofs.close();

    // 3) Запускаем клиента
    int ret = system("./src/client/pgw_client cfg_cli.json 001010123456789 > out.txt");
    ASSERT_EQ(WEXITSTATUS(ret), 0);

    // 4) Проверяем вывод
    std::ifstream out("out.txt");
    std::string resp;
    std::getline(out, resp);
    EXPECT_EQ(resp, "rejected");

    srv.join();
    std::remove("cfg_cli.json");
    std::remove("cli.log");
    std::remove("out.txt");
}
