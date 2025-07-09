#pragma once

#ifdef _WIN32
#include <winsock2.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#endif

#include <chrono>
#include <string>
#include <vector>

#include "common.h"

// Estrutura para armazenar informações do cliente
struct ClientInfo {
    sockaddr_in address;     // Armazena o endereço + porta do cliente
    socklen_t address_len;   // Tamanho da estrutura 'sockaddr_in'
    std::string name;        // Nome do cliente
    bool is_active = false;  // Indica se o cliente está ativo

    // Armazena o tempo do último pacote recebido do cliente
    std::chrono::steady_clock::time_point last_packet_time;
};

// Processa um pacote recebido de um cliente.
void handle_received_packet(int sock, const std::vector<char>& buffer,
                            const sockaddr_in& sender_addr,
                            socklen_t sender_len, ClientInfo clients[2]);

// Verifica se o cliente está inativo e desconecta se necessário
void check_client_timeouts(int sock, ClientInfo clients[2]);

//  Imprime informações do cliente
void print_client_info(const std::string& message,
                       const sockaddr_in& client_addr,
                       const std::string& name = "");