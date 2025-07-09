#include "server_handler.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#endif

#include <atomic>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

std::atomic<bool> running;

// Gerencia o loop principal do servidor
void server_loop(int sock) {
    std::cout << "Servidor de áudio iniciado na porta " << PORT
              << ". Pressione Enter para encerrar." << std::endl;

    // Vetor para armazenar informações dos clientes
    ClientInfo clients[2] = {};

    // Buffer para receber pacotes de áudio
    // (O primeiro byte é usado para indicar o tipo de pacote)
    std::vector<char> buffer(1 + AUDIO_BUFFER_SIZE);

    // Loop principal do servidor
    while (running) {
        // fd_set é uma estrutura usada pela função select() para monitorar
        // sockets
        fd_set read_fds;
        FD_ZERO(&read_fds);       // Limpa o conjunto de sockets
        FD_SET(sock, &read_fds);  // Adiciona o socket do servidor ao conjunto

        // Define o tempo de espera para a função select()
        // Se nada acontecer em 1 segundo, a função retorna
        struct timeval tv = {1, 0};

        // Select aguarda atividade no socket do servidor
        // ou até que o tempo limite expire
        int activity = select(sock + 1, &read_fds, nullptr, nullptr, &tv);

        // Se houver atividade no socket do servidor
        if (activity > 0) {
            // Armazena as informaçÕes de quem enviou o pacote
            sockaddr_in sender_addr{};
            socklen_t len = sizeof(sender_addr);

            // Recebe o pacote de áudio do cliente e armazena no buffer
            ssize_t n = recvfrom(sock, buffer.data(), buffer.size(), 0,
                                 (sockaddr*)&sender_addr, &len);

            // Se recebeu um pacote envia para a função de tratamento
            if (n > 0) {
                std::string_view packet_view(buffer.data(), n);
                handle_received_packet(sock, packet_view, sender_addr, len,
                                       clients);
            }
        }

        // Verifica se os clientes estão inativos e desconecta se necessário
        check_client_timeouts(sock, clients);
    }
}

// Verifica se dois endereços de cliente são iguais
bool are_addresses_equal(const sockaddr_in& addr1, const sockaddr_in& addr2) {
    return addr1.sin_addr.s_addr == addr2.sin_addr.s_addr &&
           addr1.sin_port == addr2.sin_port;
}

// Função para notificar todos os clientes
void broadcast_server_message(int sock, const std::string& message,
                              ClientInfo clients[2],
                              int exclude_client_index = -1) {
    // Declara um pacote de mensagem do servidor
    std::vector<char> msg_packet;
    msg_packet.reserve(1 + message.size());
    msg_packet.push_back(SERVER_MESSAGE);  // Tipo de pacote
    // Insere a mensagem no pacote, a partir do segundo byte
    msg_packet.insert(msg_packet.end(), message.begin(), message.end());

    // Envia a mensagem para todos os clientes, exceto o cliente excluído
    for (int i = 0; i < 2; ++i) {
        if (i != exclude_client_index && clients[i].is_active) {
            sendto(sock, msg_packet.data(), msg_packet.size(), 0,
                   (sockaddr*)&clients[i].address, clients[i].address_len);
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
        const char login_ok_packet = LOGIN_OK;
        sendto(sock, &login_ok_packet, sizeof(login_ok_packet), 0,
               (sockaddr*)&sender_addr, sender_len);

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
            sendto(sock, msg_packet.data(), msg_packet.size(), 0,
                   (sockaddr*)&sender_addr, sender_len);
        }
    }
    // Caso não haja slot livre, informa que o servidor está cheio
    else {
        const char server_full_packet = SERVER_FULL;
        sendto(sock, &server_full_packet, sizeof(server_full_packet), 0,
               (sockaddr*)&sender_addr, sender_len);
        print_client_info("Tentativa de conexão rejeitada (servidor cheio):",
                          sender_addr, std::string(name));
    }
}

// Processa um pacote de áudio
void process_audio_data(int sock, std::string_view audio_packet,
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
        sendto(sock, audio_packet.data(), audio_packet.size(), 0,
               (sockaddr*)&clients[receiver_idx].address,
               clients[receiver_idx].address_len);
    } else {
        char pong_packet = KEEPALIVE_PONG;
        sendto(sock, &pong_packet, sizeof(pong_packet), 0,
               (sockaddr*)&sender_addr, sizeof(sender_addr));
    }
}

// Lida com pacotes recebidos e retransmite para os clientes conectados
void handle_received_packet(int sock, const std::string_view& buffer,
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
            process_audio_data(sock, buffer, sender_addr, clients);
            break;
        // Pacote de descobrimento
        case DISCOVERY_REQUEST: {
            std::cout << "Recebido pedido de descoberta de ";
            print_client_info("", sender_addr);

            char response_packet = DISCOVERY_RESPONSE;
            sendto(sock, &response_packet, sizeof(response_packet), 0,
                   (sockaddr*)&sender_addr, sender_len);
            break;
        }
        // Pacote de logout
        case LOGOUT_NOTICE: {
            for (int i = 0; i < 2; ++i) {
                if (clients[i].is_active &&
                    are_addresses_equal(clients[i].address, sender_addr)) {
                    print_client_info("Cliente desconectado (logout):",
                                      clients[i].address, clients[i].name);

                    std::string leave_msg =
                        "[SERVER] '" + clients[i].name + "' saiu da chamada.";
                    clients[i].is_active = false;

                    broadcast_server_message(sock, leave_msg, clients);
                    break;
                }
            }
            break;
        }
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