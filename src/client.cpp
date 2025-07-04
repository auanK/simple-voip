#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "audio.h"
#include "common.h"

// Variável que armazena pacotes de áudio recebidos.
// Cada pacote é um vetor de caracteres, representando um bloco de áudio.
// A thread de recebimento coloca pacotes aqui, e a thread de playback
// retira para tocar.
std::queue<std::vector<char>> jitter_buffer;

// A variável mutex bloqueia o acesso ao jitter_buffer enquanto uma
// thread está adicionando ou removendo pacotes, garantindo que apenas uma
// thread possa modificar a fila por vez, evitando erros como corrupção de
// dados ou comportamento inesperado.
std::mutex jitter_buffer_mutex;

// A thread de recebimento toca a campainha quando um novo pacote chega,
// acordando a thread de playback para que ela possa processar o pacote.
// Evita que a thread de playback fique acordada o tempo todo, economizando
// recursos do sistema.
std::condition_variable cv;

// Interruptor geral de todas as threads.
std::atomic<bool> running(false);

// Instância do motor de áudio em audio.h, responsável por capturar e
// reproduzir áudio.
AudioHandler audio_handler;

// Função responsável por capturar áudio do microfone e enviá-lo para o
// servidor, recebe o socket e o endereço do servidor como parâmetros.
void send_thread_func(int sockfd, struct sockaddr_in server_addr);

// Função responsável por receber pacotes de áudio do servidor e
// colocá-los no jitter buffer, que é uma fila de pacotes a serem
// reproduzidos. Ela recebe o socket como parâmetro.
void receive_thread_func(int sockfd);

// Função responsável por tocar o áudio recebido do jitter buffer.
// Ela não recebe parâmetros, pois acessa o jitter buffer e o motor de áudio
// diretamente.
void playback_thread_func();

// Função principal do cliente
int main(int argc, char* argv[]) {
    // Verifica se o endereço IP do servidor foi passado como argumento.
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <endereco IP do servidor>\n", argv[0]);
        return 1;
    }

    // Tenta inicializar o motor de áudio.
    if (!audio_handler.init()) {
        std::cerr << "Erro ao inicializar o AudioHandler." << std::endl;
        return 1;
    }

    // Cria o socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar o socket");
        return 1;
    }

    // Define a estrutura sockaddr_in para o servidor.
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;    // Define o tipo de endereço (IPv4)
    server_addr.sin_port = htons(PORT);  // Define a porta do servidor
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);

    sendto(sockfd, "H", 1, 0, (struct sockaddr*)&server_addr,
           sizeof(server_addr));
    std::cout << "Conectado ao servidor. Iniciando comunicação..." << std::endl;

    // Flag para controlar a execução das threads.
    running = true;

    // Inicia as threads de envio, recebimento e reprodução de áudio.
    // Cada thread é responsável por uma parte do processo de comunicação:
    std::thread sender(send_thread_func, sockfd, server_addr);
    std::thread receiver(receive_thread_func, sockfd);
    std::thread player(playback_thread_func);

    // Aguardando o usuário pressionar Enter para encerrar o programa.
    std::cout << "\n*** Comunicação ativa. Pressione Enter para sair. ***\n";
    getchar();

    std::cout << "Encerrando as threads..." << std::endl;

    // Com a flag em false os loops das threads vão terminar.
    running = false;

    // Caso a thread de playback esteja esperando por novos pacotes,
    // notifica-a para que ela possa sair do loop.
    cv.notify_all();

    // Fecha o socket de envio, para que a thread de envio não envie mais
    // pacotes.
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    // Aguarda as threads terminarem.
    // A função join bloqueia a thread principal até que a thread especificada
    // termine sua execução.
    sender.join();
    receiver.join();
    player.join();

    // Finaliza o programa, liberando os recursos utilizados.
    audio_handler.terminate();
    std::cout << "Recursos liberados. Programa encerrado." << std::endl;

    return 0;
}

// Função responsável por capturar áudio do microfone e enviá-lo para o
// servidor, recebe o socket e o endereço do servidor como parâmetros.
void send_thread_func(int sockfd, struct sockaddr_in server_addr) {
    // Buffer para armazenar os dados de áudio capturados.
    std::vector<char> buffer(AUDIO_BUFFER_SIZE);

    // Inicia a captura de áudio do microfone.
    audio_handler.startCapture();
    std::cout << "Microfone ativado. Comece a falar..." << std::endl;

    // Loop principal
    while (running) {
        // Lê um bloco de áudio do microfone e armazena no buffer.
        audio_handler.read(buffer.data());

        // Envia o buffer de áudio para o servidor via UDP.
        if (running) {
            sendto(sockfd, buffer.data(), buffer.size(), 0,
                   (sockaddr*)&server_addr, sizeof(server_addr));
        }
    }

    // Para a captura de áudio quando o loop termina.
    audio_handler.stopCapture();
    std::cout << "Captura de áudio terminada." << std::endl;
}

// Função responsável por receber pacotes de áudio do servidor e
// colocá-los no jitter buffer, que é uma fila de pacotes a serem
// reproduzidos. Ela recebe o socket como parâmetro.
void receive_thread_func(int sockfd) {
    // Buffer para armazenar os dados recebidos do servidor.
    std::vector<char> buffer(AUDIO_BUFFER_SIZE);

    sockaddr_in sender_addr;
    socklen_t len = sizeof(sender_addr);

    // Loop principal
    while (running) {
        // Recebe pacotes de áudio do servidor via UDP, função 'recvfrom'
        // bloqueia até que um pacote chegue.
        // Ela preenche o buffer com os dados recebidos e também preenche
        // 'sender_addr' com o endereço do remetente.
        ssize_t n = recvfrom(sockfd, buffer.data(), AUDIO_BUFFER_SIZE, 0,
                             (sockaddr*)&sender_addr, &len);

        // Se um pacote válido foi recebido.
        if (n > 0) {
            {
                // lock_guard tranca o mutex no início do bloco e destranca
                // automaticamente no final.
                std::lock_guard<std::mutex> lock(jitter_buffer_mutex);

                // Adiciona o pacote recebido ao final da fila
                jitter_buffer.push(
                    std::vector<char>(buffer.begin(), buffer.begin() + n));
            }

            // Avisa a thread de playback que há novos pacotes disponíveis.
            cv.notify_one();
        }
    }
    std::cout << "Recebimento de áudio terminado." << std::endl;
}

// Função responsável por tocar o áudio recebido do jitter buffer.
void playback_thread_func() {
    // Buffer para armazenar os dados de áudio a serem reproduzidos.
    std::vector<char> buffer(AUDIO_BUFFER_SIZE, 0);

    // Inicia a reprodução de áudio nos alto-falantes.
    audio_handler.startPlayback();
    std::cout << "Alto-falantes ativados. Aguardando áudio..." << std::endl;

    while (running) {
        {
            // lock_guard tranca o mutex no início do bloco e destranca
            // automaticamente no final.
            std::unique_lock<std::mutex> lock(jitter_buffer_mutex);

            // Aguarda por até 20ms por novos pacotes no jitter_buffer usando
            // a 'campainha' que toca na thread de recebimento.
            if (cv.wait_for(lock, std::chrono::milliseconds(20),
                            [] { return !jitter_buffer.empty(); })) {
                // Se acordou e há pacotes, salva o primeiro pacote e
                // remove-o da fila.
                buffer = jitter_buffer.front();
                jitter_buffer.pop();
            } else {
                // Se acordou pelo timeout de 20ms e a fila ainda está vazia,
                // toca silêncio. Isso cria uma experiência de áudio mais suave
                // em caso de pequenas perdas de pacotes.
                std::fill(buffer.begin(), buffer.end(), 0);
            }
        }

        // Caso a variável 'running' ainda esteja ativa, escreve o buffer
        // de áudio nos alto-falantes.
        if (running) {
            audio_handler.write(buffer.data());
        }
    }
    // Para a reprodução de áudio quando o loop termina.
    audio_handler.stopPlayback();
    std::cout << "Reprodução de áudio terminada." << std::endl;
}