#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "common.h"

// Estrutura para armazenar informações do cliente
struct ClientInfo {
    sockaddr_in address;    // Armazena o endereço + porta do cliente
    socklen_t address_len;  // Tamanho da estrutura 'sockaddr_in'

    // Armazena o tempo do último pacote recebido do cliente
    std::chrono::steady_clock::time_point last_packet_time;
};

// Função para lidar com pacotes recebidos
void handle_received_packet(int sock, char* buffer, ClientInfo& client1,
                            bool& client1_active, ClientInfo& client2,
                            bool& client2_active);

// Função para remover clientes inativos
void check_client_timeouts(ClientInfo& client, bool& client_active,
                           const int timeout_seconds, const int client_num);

// Verifica se dois endereços de cliente são iguais
bool are_addresses_equal(const sockaddr_in& addr1, const sockaddr_in& addr2);

// Imprime informações do cliente
void print_client_info(const std::string& message,
                       const sockaddr_in& client_addr);

// Função principal do servidor
int main() {
    // Cria o socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Erro ao criar o socket");
        return 1;  // Encerra com erro
    }

    // Define a estrutura sockaddr_in para o servidor
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;  // Define o tipo de endereço (IPv4)
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Ouve qualquer IP da máquina
    server_addr.sin_port = htons(PORT);        // Define a porta do servidor

    // Tenta bindar o socket ao endereço e porta especificados
    if (bind(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro ao vincular o socket");
        close(sock);  // Fecha o socket antes de sair
        return 1;     // Encerra com erro
    }

    std::cout << "Servidor de áudio iniciado na porta " << PORT
              << ". Aguardando clientes..." << std::endl;

    // Variáveis para gerenciar os dois slots de clientes
    ClientInfo client1, client2;
    bool client1_active = false, client2_active = false;

    const int TIMEOUT_SECONDS = 10;  // Tempo limite para inatividade do cliente

    char buffer[AUDIO_BUFFER_SIZE];  // Buffer para armazenar os dados recebidos

    // Loop principal do servidor
    while (true) {
        // Descritor para monitorar a atividade do socketu
        fd_set read_fds;

        FD_ZERO(&read_fds);  // Limpa o conjunto de descritores

        // Adiciona o socket ao conjunto de descritores
        // Isso permite que o select monitore a atividade no socket
        FD_SET(sock, &read_fds);

        // Cria uma estrutura para determinar quanto tempo esperar monitorando o
        // socket
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        // Espera por atividade no socket, com timeout de 1 segundo
        int activity = select(sock + 1, &read_fds, NULL, NULL, &tv);

        // Se houver aticidade lemos o pacote recebido
        if (activity > 0) {
            handle_received_packet(sock, buffer, client1, client1_active,
                                   client2, client2_active);
        }

        // Verifica se os clientes estão inativos e os desconecta se necessário
        check_client_timeouts(client1, client1_active, TIMEOUT_SECONDS, 1);
        check_client_timeouts(client2, client2_active, TIMEOUT_SECONDS, 2);
    }

    close(sock);
    return 0;
}

// Lida com pacotes recebidos e retransmite para os clientes conectados
void handle_received_packet(int sock, char* buffer, ClientInfo& client1,
                            bool& client1_active, ClientInfo& client2,
                            bool& client2_active) {
    sockaddr_in sender_addr{};
    socklen_t len = sizeof(sender_addr);

    // Armazena os dados recebidos no buffer (com o recvfrom) e salva o
    // tamanho do pacote recebido
    ssize_t n = recvfrom(sock, buffer, AUDIO_BUFFER_SIZE, 0,
                         (sockaddr*)&sender_addr, &len);

    // Nenhum dado válido recebido
    if (n <= 0) {
        return;
    }

    auto now = std::chrono::steady_clock::now();  // Pega o tempo atual

    // Caso 1: O pacote recebido é do cliente 1
    if (client1_active && are_addresses_equal(sender_addr, client1.address)) {
        // Atualiza o tempo do último pacote recebido
        client1.last_packet_time = now;

        // Se o cliente 2 está ativo, retransmite o pacote para ele
        if (client2_active) {
            sendto(sock, buffer, n, 0, (sockaddr*)&client2.address,
                   client2.address_len);
        }
        return;
    }

    // Caso 2: O pacote recebido é do cliente 2
    if (client2_active && are_addresses_equal(sender_addr, client2.address)) {
        // Atualiza o tempo do último pacote recebido
        client2.last_packet_time = now;

        // Se o cliente 1 está ativo, retransmite o pacote para ele
        if (client1_active) {
            sendto(sock, buffer, n, 0, (sockaddr*)&client1.address,
                   client1.address_len);
        }
        return;
    }

    // Caso 3: O pacote recebido é de um novo cliente e existe espaço pois não
    // caiu no caso 1 ou 2

    // Caso 3.1: Checa se vai ocupar o lugar do cliente 1
    if (!client1_active) {
        print_client_info("Cliente 1 conectado: ", sender_addr);
        client1 = {sender_addr, len, now};  // Seta o endereço do cliente 1
        client1_active = true;              // Marca o cliente 1 como ativo
        return;
    }

    // Caso 3.2: Checa se vai ocupar o lugar do cliente 2
    if (!client2_active) {
        print_client_info("Cliente 2 conectado: ", sender_addr);
        client2 = {sender_addr, len, now};  // Seta o endereço do cliente 2
        client2_active = true;              // Marca o cliente 2 como ativo
        return;
    }
}

// Verifica se o cliente está inativo e desconecta se necessário
void check_client_timeouts(ClientInfo& client, bool& client_active,
                           const int timeout_seconds, const int client_num) {
    // Se o cliente não está ativo, não faz nada
    if (!client_active) {
        return;
    }

    // Pega o tempo atual e verifica se o tempo de inatividade do cliente
    // excede o limite definido
    auto now = std::chrono::steady_clock::now();
    if (now - client.last_packet_time > std::chrono::seconds(timeout_seconds)) {
        std::cout << "Cliente " << client_num
                  << " desconectado por inatividade: ";
        print_client_info("", client.address);

        client_active = false;  // Marca o cliente como inativo
    }
}

// Verifica se dois endereços de cliente são iguais
bool are_addresses_equal(const sockaddr_in& addr1, const sockaddr_in& addr2) {
    return addr1.sin_addr.s_addr == addr2.sin_addr.s_addr &&
           addr1.sin_port == addr2.sin_port;
}

//  Imprime informações do cliente
void print_client_info(const std::string& message,
                       const sockaddr_in& client_addr) {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    std::cout << message << ip_str << ":" << ntohs(client_addr.sin_port)
              << std::endl;
}