#include "client_handler.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <atomic>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <string_view>
#include <vector>

#include "common.h"

// Definição das variáveis globais (Documentação em client_utils.h)

std::atomic<bool> running(false);
std::queue<std::vector<char>> jitter_buffer;
std::mutex jitter_buffer_mutex;
std::condition_variable jitter_buffer_cond;

// Instância do motor de áudio em audio.h, responsável por capturar e reproduzir
// áudio.
AudioHandler audio_handler;

// Função para descobrir o IP do servidor na rede local.
std::string discover_server_on_network() {
    std::cout << "IP do servidor não fornecido. Procurando na rede local..."
              << std::endl;

    // Cria um socket para enviar pacotes de broadcast
    int broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (broadcast_sock < 0) {
        perror("Erro ao criar socket de broadcast");
        return "";
    }

    // Habilitar a opção de broadcast no socket
    int broadcast_enable = 1;
#ifdef _WIN32
    if (setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST,
                   (const char*)&broadcast_enable,
                   sizeof(broadcast_enable)) < 0) {
#else
    if (setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable,
                   sizeof(broadcast_enable)) < 0) {
#endif
        perror("Erro ao configurar socket para broadcast");
#ifdef _WIN32
        closesocket(broadcast_sock);
#else
        close(broadcast_sock);
#endif
        return "";
    }

    // Definir um timeout para a resposta do servidor
#ifdef _WIN32
    DWORD timeout = DISCOVERY_TIMEOUT_SEC * 1000;
    setsockopt(broadcast_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout,
               sizeof(timeout));
#else
    // Linux espera uma struct timeval
    struct timeval tv = {DISCOVERY_TIMEOUT_SEC, 0};
    setsockopt(broadcast_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    // Declara a estrutura de endereço para o broadcast
    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;    // Define o tipo de endereço (IPv4)
    broadcast_addr.sin_port = htons(PORT);  // Define a porta do servidor
    // Endereço de broadcast
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    // Monta e envia o pacote de descoberta.
    // O primeiro byte é o tipo do pacote (DISCOVERY_REQUEST) e o restante é
    // vazio, pois não há dados adicionais.
    char discovery_packet = DISCOVERY_REQUEST;
    sendto(broadcast_sock, &discovery_packet, sizeof(discovery_packet), 0,
           (sockaddr*)&broadcast_addr, sizeof(broadcast_addr));

    // Monta um buffer para receber a resposta do servidor.
    char response_buffer[1];
    // Estrutura para armazenar o endereço do servidor que respondeu.
    sockaddr_in server_response_addr{};
    socklen_t server_addr_len = sizeof(server_response_addr);

    // Espera pela resposta do servidor, preenche o buffer de resposta e salva
    // o endereço do servidor que respondeu.
    ssize_t n =
        recvfrom(broadcast_sock, response_buffer, sizeof(response_buffer), 0,
                 (sockaddr*)&server_response_addr, &server_addr_len);

// Fecha o socket de broadcast após receber a resposta ou timeout.
#ifdef _WIN32
    closesocket(broadcast_sock);
#else
    close(broadcast_sock);
#endif

    // Se foi um pacote válido e o primeiro byte é DISCOVERY_RESPONSE,
    // extrai o IP do servidor e retorna.
    if (n > 0 && response_buffer[0] == DISCOVERY_RESPONSE) {
        std::string server_ip = inet_ntoa(server_response_addr.sin_addr);
        std::cout << "Servidor encontrado em: " << server_ip << std::endl;
        return server_ip;
    }

    // Se não foi recebido nenhum pacote válido, imprime uma mensagem de erro.
    std::cerr << "Nenhum servidor encontrado na rede." << std::endl;
    return "";
}

#ifdef __linux__
#include <fcntl.h>
// Função para suprimir erros do ALSA.
void suppress_alsa_errors(bool suppress) {
    static int stderr_original_fd = -1;
    if (suppress) {
        fflush(stderr);
        stderr_original_fd = dup(STDERR_FILENO);
        int dev_null_fd = open("/dev/null", O_WRONLY);
        dup2(dev_null_fd, STDERR_FILENO);
        close(dev_null_fd);
    } else if (stderr_original_fd != -1) {
        fflush(stderr);
        dup2(stderr_original_fd, STDERR_FILENO);
        close(stderr_original_fd);
    }
}
#endif

// Thread que envia áudio para o servidor
void send_thread_func(int sock, const sockaddr_in& server_addr) {
    // Buffer para armazenar os dados de áudio capturados.
    std::vector<char> audio_packet(1 + AUDIO_BUFFER_SIZE);
    audio_packet[0] = AUDIO_DATA;

    // Inicia a captura de áudio do microfone.
    audio_handler.startCapture();
    std::cout << "Microfone ativado." << std::endl;

    // Loop principal
    while (running) {
        // Lê um bloco de áudio do microfone e armazena no buffer.
        audio_handler.read(audio_packet.data() + 1);

        // Envia o buffer de áudio para o servidor via UDP.
        sendto(sock, audio_packet.data(), audio_packet.size(), 0,
               (sockaddr*)&server_addr, sizeof(server_addr));
    }

    // Para a captura de áudio quando o loop termina.
    audio_handler.stopCapture();
    std::cout << "Captura de áudio terminada." << std::endl;
}

// Thread que recebe dados do servidor
void receive_thread_func(int sock, std::promise<void> connection_promise) {
    // Buffer para armazenar os dados recebidos do servidor.
    std::vector<char> receive_buffer(1 + AUDIO_BUFFER_SIZE);
    // Flag para verificar se a conexão foi confirmada.
    bool connection_confirmed = false;

    // Loop principal
    while (running) {
        // Recebe pacotes de áudio do servidor via UDP, função 'recvfrom'
        // bloqueia até que um pacote chegue (ou o timeout em client.cpp
        // expirar).
        ssize_t n = recvfrom(sock, receive_buffer.data(), receive_buffer.size(),
                             0, nullptr, nullptr);

        // Se nenhum pacote válido foi recebido.
        if (n <= 0) {
            // Verifica a causa do erro.
            bool is_timeout = false;
#ifdef _WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                is_timeout = true;
            }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                is_timeout = true;
            }
#endif
            // A conexão caiu
            if (connection_confirmed) {
                if (is_timeout) {
                    std::cerr
                        << "\n[INFO] Conexão perdida. O servidor desconectou."
                        << std::endl;
                } else {
                    perror("Erro de rede fatal. Encerrando");
                }
                std::exit(1);
            }
            // A tentativa de conexão inicial falhou.
            else {
                if (is_timeout) {
                    std::cerr << "\n[FALHA] O servidor não respondeu."
                              << std::endl;
                } else {
                    perror("Erro em recvfrom");
                }

                // Notifica a main thread sobre a falha na conexão inicial.
                try {
                    connection_promise.set_exception(std::make_exception_ptr(
                        std::runtime_error("Falha ao conectar")));
                } catch (const std::future_error&) {
                }
            }

            // Declara que o programa não deve continuar
            running = false;
            break;
        }

        // Se um pacote válido foi recebido, processa o pacote.
        // Pega o primeiro byte do buffer como o tipo de pacote e
        // o restante como os dados do pacote.
        PacketType type = static_cast<PacketType>(receive_buffer[0]);
        std::string_view data_view(receive_buffer.data() + 1, n - 1);

        // Verifica se a conexão foi confirmada.
        // Se o tipo do pacote for LOGIN_OK ou SERVER_MESSAGE, a conexão
        // é considerada estabelecida.
        if (!connection_confirmed &&
            (type == LOGIN_OK || type == SERVER_MESSAGE)) {
            connection_confirmed = true;
            std::cout << "\n*** Conexão estabelecida! ***" << std::endl
                      << "Pressione Enter para encerrar." << std::endl;

            // Cumpre a promessa para notificar a thread principal
            connection_promise.set_value();
        }

        // Com base no tipo do pacote, processa os dados recebidos.
        switch (type) {
            // Caso seja um pacote de áudio, adiciona ao jitter buffer.
            case AUDIO_DATA: {
                // lock_guard tranca o mutex no início do bloco e destranca
                // automaticamente no final.
                std::lock_guard<std::mutex> lock(jitter_buffer_mutex);

                // Adiciona o pacote recebido ao final da fila
                jitter_buffer.emplace(data_view.begin(), data_view.end());

                // Avisa a thread de playback que há novos pacotes disponíveis.
                jitter_buffer_cond.notify_one();
                break;
            }
            // Imprime uma mensagem do servidor.
            case SERVER_MESSAGE:
                std::cout << data_view << std::endl;
                break;
            // Imprime a mensagem de servidor cheio e encerra o cliente.
            case SERVER_FULL:
                std::cerr << "[INFO] O servidor está cheio." << std::endl;
                try {
                    connection_promise.set_exception(std::make_exception_ptr(
                        std::runtime_error("Servidor cheio")));
                } catch (const std::future_error&) {
                }

                running = false;
                break;
            // Caso seja um pacote de ping, ignora.
            case KEEPALIVE_PONG:
                break;
            default:
                break;
        }
    }
    jitter_buffer_cond.notify_all();  // Notifica a thread de playback
    std::cout << "Recepção de áudio terminada." << std::endl;
}

// Thread que reproduz o áudio recebido
void playback_thread_func() {
    // Buffer para armazenar os dados de áudio a serem reproduzidos.
    std::vector<char> buffer(AUDIO_BUFFER_SIZE, 0);

    // Inicia a reprodução de áudio nos alto-falantes.
    audio_handler.startPlayback();
    std::cout << "Alto-falantes ativados." << std::endl;

    while (running || !jitter_buffer.empty()) {
        {
            // lock_guard tranca o mutex no início do bloco e destranca
            // automaticamente no final.
            std::unique_lock<std::mutex> lock(jitter_buffer_mutex);

            // A thread dorme até ser notificada, quando o buffer não está vazio
            // ou o programa está encerrando.
            jitter_buffer_cond.wait(
                lock, [] { return !jitter_buffer.empty() || !running; });

            // Se a thread acordou e não está mais rodando, e não contém
            // pacotes no jitter_buffer, sai do loop.
            if (!running && jitter_buffer.empty()) break;

            // Se há pacotes no jitter_buffer
            if (!jitter_buffer.empty()) {
                // Peega o primeiro pacote da fila.
                const auto& front = jitter_buffer.front();

                // Verifica se o pacote tem o tamanho correto.
                if (front.size() == AUDIO_BUFFER_SIZE) {
                    // Correto: Copia os dados do pacote para o buffer de
                    // reprodução.
                    std::copy(front.begin(), front.end(), buffer.begin());
                } else {
                    // Incorreto: Copia silêncio para o buffer de reprodução.
                    std::fill(buffer.begin(), buffer.end(), 0);
                }
                jitter_buffer.pop();  // Remove o pacote processado da fila
            } else {
                // Se não há pacotes, preenche o buffer com silêncio
                std::fill(buffer.begin(), buffer.end(), 0);
            }
        }

        // Envia o buffer de áudio para os alto-falantes.
        audio_handler.write(buffer.data());
    }

    // Para a reprodução de áudio quando o loop termina.
    audio_handler.stopPlayback();
    std::cout << "Reprodução de áudio terminada." << std::endl;
}