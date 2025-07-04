#include "client_handler.h"

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <condition_variable>
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
std::condition_variable cv;

// Instância do motor de áudio em audio.h, responsável por capturar e reproduzir
// áudio.
AudioHandler audio_handler;

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
void receive_thread_func(int sock) {
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
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::cerr << (connection_confirmed
                                  ? "\n[INFO] Desconectado por inatividade."
                                  : "[FALHA] Servidor não respondeu.")
                          << std::endl;
            }
            // Desconecta o cliente como consequência.
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
            std::cout << "\n*** Conexão estabelecida! ***" << std::endl;
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
                cv.notify_one();
                break;
            }
            // Imprime uma mensagem do servidor.
            case SERVER_MESSAGE:
                std::cout << data_view << std::endl;
                break;
            // Imprime a mensagem de servidor cheio e encerra o cliente.
            case SERVER_FULL:
                std::cerr << "[INFO] O servidor está cheio." << std::endl;
                running = false;
                break;
            default:
                break;
        }
    }
    cv.notify_all();  // Notifica a todas as threads
    std::cout << "Recebimento de áudio terminado." << std::endl;
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
            cv.wait(lock, [] { return !jitter_buffer.empty() || !running; });

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