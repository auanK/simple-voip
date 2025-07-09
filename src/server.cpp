#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

using socklen_t = int;
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

#include "common.h"
#include "server_handler.h"

// Função principal do servidor
int main() {
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return 1;
    }
#endif

    // Cria o socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Erro ao criar o socket");
        return 1;
    }

    // Define a estrutura sockaddr_in para o servidor
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;  // Define o tipo de endereço (IPv4)
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Ouve qualquer IP da máquina
    server_addr.sin_port = htons(PORT);        // Define a porta do servidor

    // Tenta bindar o socket ao endereço e porta especificados
    if (bind(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro ao vincular o socket");
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return 1;
    }

    // Inicia o loop do servidor em uma thread separada
    running = true;
    std::thread server_thread(server_loop, sock);

    // Aguarda o Enter do utilizador
    std::cin.get();

    std::cout << "A encerrar o servidor..." << std::endl;
    running = false;

    // Aguarda a thread do servidor terminar
    server_thread.join();

// Fecha o socket antes de sair
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    std::cout << "Servidor encerrado." << std::endl;

    return 0;
}