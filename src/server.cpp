#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <iostream>
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

    std::cout << "Servidor de áudio iniciado na porta " << PORT
              << ". Aguardando clientes..." << std::endl;

    // Vetor para armazenar informações dos clientes
    ClientInfo clients[2] = {};

    // Buffer para receber pacotes de áudio
    // (O primeiro byte é usado para indicar o tipo de pacote)
    std::vector<char> buffer(1 + AUDIO_BUFFER_SIZE);

    // Loop principal do servidor
    while (true) {
        // fd_set é uma estrutura usada pela função select() para monitorar
        // sockets
        fd_set read_fds;
        FD_ZERO(&read_fds);       // Limpa o conjunto de sockets
        FD_SET(sock, &read_fds);  // Adiciona o socket do servidor ao conjunto

        // Define o tempo de espera para a função select()
        // Se nada acontecer em 1 segundo, select() retorna
        struct timeval tv = {1, 0};

        // select() bloqueia o programa até que haja dados para ler
        // ou até que o tempo limite expire
        if (select(sock + 1, &read_fds, nullptr, nullptr, &tv) > 0) {
            // Armazena as informações de quem enviou o pacote
            sockaddr_in sender_addr{};
            socklen_t len = sizeof(sender_addr);

            // Recebe o pacote de áudio do cliente e armazena no buffer
            ssize_t n = recvfrom(sock, buffer.data(), buffer.size(), 0,
                                 (sockaddr*)&sender_addr, &len);

            // Se os dados foram recebidos com sucesso
            if (n > 0) {
                // Envia o pacote recebido para a função que lida com o pacote
                std::vector<char> packet(buffer.begin(), buffer.begin() + n);
                handle_received_packet(sock, packet, sender_addr, len, clients);
            }
        }

        // Verifica se os clientes estão inativos e os desconecta se necessário
        check_client_timeouts(sock, clients);
    }

// Fecha o socket antes de sair
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    return 0;
}