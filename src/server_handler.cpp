#include "server_handler.h"

#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string_view>

// Verifica se dois endereços de cliente são iguais
bool are_addresses_equal(const sockaddr_in& addr1, const sockaddr_in& addr2) {
    return addr1.sin_addr.s_addr == addr2.sin_addr.s_addr &&
           addr1.sin_port == addr2.sin_port;
}

// Função auxiliar para enviar pacotes
void send_packet(int sock, const std::vector<char>& packet,
                 const sockaddr_in& dest_addr, socklen_t dest_len) {
    sendto(sock, packet.data(), packet.size(), 0, (sockaddr*)&dest_addr,
           dest_len);
}

// Função para notificar todos os clientes
void broadcast_server_message(int sock, const std::string& message,
                              ClientInfo clients[2],
                              int exclude_client_index = -1) {
    // Declara um pacote de mensagem do servidor
    std::vector<char> msg_packet;
    msg_packet.push_back(SERVER_MESSAGE);  // Tipo de pacote
    // Insere a mensagem no pacote, a partir do segundo byte
    msg_packet.insert(msg_packet.end(), message.begin(), message.end());

    // Envia a mensagem para todos os clientes, exceto o cliente excluído
    for (int i = 0; i < 2; ++i) {
        if (i != exclude_client_index && clients[i].is_active) {
            send_packet(sock, msg_packet, clients[i].address,
                        clients[i].address_len);
        }
    }
}

// Lida com uma tentativa de conexão de um novo cliente
void process_login(int sock, std::string_view name,
                   const sockaddr_in& sender_addr, socklen_t sender_len,
                   ClientInfo clients[2]) {
    // Verifica se há slot disponível e atribui o slot livre ao novo cliente
    int free_slot = -1;
    for (int i = 0; i < 2; ++i) {
        if (!clients[i].is_active) {
            free_slot = i;
            break;
        }
    }

    // Se houver um slot livre, preenche as informações do cliente
    if (free_slot != -1) {
        clients[free_slot].address = sender_addr;
        clients[free_slot].address_len = sender_len;
        clients[free_slot].name = name;
        clients[free_slot].last_packet_time = std::chrono::steady_clock::now();
        clients[free_slot].is_active = true;

        print_client_info("Cliente conectado:", sender_addr, std::string(name));

        // Envia um pacote de confirmação de login para o novo cliente
        send_packet(sock, {LOGIN_OK}, sender_addr, sender_len);

        // Envia uma mensagem para todos os clientes informando sobre a nova
        // conexão
        std::string join_msg =
            "[SERVER] '" + std::string(name) + "' entrou na chamada.";
        broadcast_server_message(sock, join_msg, clients, free_slot);

        // Envia para o novo cliente que conectou quem está na chamada
        int other_client_idx = 1 - free_slot;
        if (clients[other_client_idx].is_active) {
            std::string current_user_msg =
                "[SERVER] Na chamada: '" + clients[other_client_idx].name + "'";
            std::vector<char> msg_packet;
            msg_packet.push_back(SERVER_MESSAGE);
            msg_packet.insert(msg_packet.end(), current_user_msg.begin(),
                              current_user_msg.end());
            send_packet(sock, msg_packet, sender_addr, sender_len);
        }
    }
    // Caso não haja slot livre, informa que o servidor está cheio
    else {
        send_packet(sock, {SERVER_FULL}, sender_addr, sender_len);
        print_client_info("Tentativa de conexão rejeitada (servidor cheio):",
                          sender_addr, std::string(name));
    }
}

// Processa um pacote de áudio
void process_audio_data(int sock, std::string_view audio_data,
                        const sockaddr_in& sender_addr, ClientInfo clients[2]) {
    // Encontra o índice do cliente que enviou o pacote de áudio
    int sender_idx = -1;
    for (int i = 0; i < 2; ++i) {
        if (clients[i].is_active &&
            are_addresses_equal(clients[i].address, sender_addr)) {
            sender_idx = i;
            break;
        }
    }

    // O pacote veio de um cliente desconhecido ou inativo
    if (sender_idx == -1) return;

    // Atualiza o tempo do último pacote recebido do cliente
    clients[sender_idx].last_packet_time = std::chrono::steady_clock::now();

    // Encontra o índice do outro cliente para retransmitir o áudio
    int receiver_idx = 1 - sender_idx;

    // Se o outro cliente está ativo, retransmite o pacote de áudio
    if (clients[receiver_idx].is_active) {
        // Pacote malformatado
        if (audio_data.size() > AUDIO_BUFFER_SIZE) {
            return;
        }

        std::vector<char> audio_packet;
        audio_packet.reserve(1 + audio_data.size());
        audio_packet.push_back(AUDIO_DATA);
        audio_packet.insert(audio_packet.end(), audio_data.begin(),
                            audio_data.end());

        send_packet(sock, audio_packet, clients[receiver_idx].address,
                    clients[receiver_idx].address_len);
    }
}

// Lida com pacotes recebidos e retransmite para os clientes conectados
void handle_received_packet(int sock, const std::vector<char>& buffer,
                            const sockaddr_in& sender_addr,
                            socklen_t sender_len, ClientInfo clients[2]) {
    // Pacote vazio ou inválido
    if (buffer.size() < 1) {
        return;
    }

    // Desmonta o tipo de pacote e os dados
    PacketType type = static_cast<PacketType>(buffer[0]);
    std::string_view data(buffer.data() + 1, buffer.size() - 1);

    // Com base no tipo de pacote, processa a ação correspondente
    switch (type) {
        // Pacote de solicitação de login
        case LOGIN_REQUEST:
            if (!data.empty() && data.size() <= MAX_NAME_LENGTH) {
                process_login(sock, data, sender_addr, sender_len, clients);
            }
            break;
        // Pacote de áudio
        case AUDIO_DATA:
            process_audio_data(sock, data, sender_addr, clients);
            break;
        default:
            break;
    }
}

// Verifica se o cliente está inativo e desconecta se necessário
void check_client_timeouts(int sock, ClientInfo clients[2]) {
    auto now = std::chrono::steady_clock::now();
    for (int i = 0; i < 2; ++i) {
        if (clients[i].is_active) {
            if (now - clients[i].last_packet_time >
                std::chrono::seconds(CLIENT_TIMEOUT_SEC)) {
                print_client_info("Cliente desconectado por inatividade:",
                                  clients[i].address, clients[i].name);
                std::string leave_msg =
                    "[SERVER] '" + clients[i].name + "' saiu da chamada.";
                clients[i].is_active = false;
                broadcast_server_message(sock, leave_msg, clients);
            }
        }
    }
}

// Imprime informações do cliente
void print_client_info(const std::string& message,
                       const sockaddr_in& client_addr,
                       const std::string& name) {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    std::cout << message << " " << ip_str << ":" << ntohs(client_addr.sin_port);
    if (!name.empty()) {
        std::cout << " (Nome: " << name << ")";
    }
    std::cout << std::endl;
}