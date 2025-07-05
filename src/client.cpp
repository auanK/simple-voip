#include <unistd.h>

#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "client_handler.h"
#include "common.h"

// Instância global do motor de áudio (definida no client_utils.cpp)
extern AudioHandler audio_handler;

// Função principal do cliente
int main(int argc, char* argv[]) {
    // Verifica se tem argumentos suficientes
    if (argc < 2) {
        std::cerr << "Uso: " << argv[0] << " <seu_nome> [IP do servidor]"
                  << std::endl;
        std::cerr << "Se o IP do servidor não for fornecido, "
                     "será feita uma busca na rede local."
                  << std::endl;
        return 1;
    }

    // Salva o nome do cliente
    const std::string client_name = argv[1];

    // Salva o IP do servidor se fornecido, ou descobre na rede local
    std::string server_ip;
    if (argc > 2) {
        server_ip = argv[2];
    } else {
        server_ip = discover_server_on_network();
        if (server_ip.empty()) {
            return 1;
        }
    }

    // Verifica se o nome do cliente é válido
    if (client_name.length() > MAX_NAME_LENGTH) {
        std::cerr << "O nome deve ter no máximo " << MAX_NAME_LENGTH
                  << " caracteres." << std::endl;
        return 1;
    }

    // Inicializa o motor de áudio (Suprime erros que a PortAudio pode gerar)
    suppress_alsa_errors(true);
    bool audio_ok = audio_handler.init();
    suppress_alsa_errors(false);

    // Verifica se a inicialização do PortAudio foi bem-sucedida.
    if (!audio_ok) {
        std::cerr << "Erro ao inicializar o PortAudio." << std::endl;
        return 1;
    }

    // Cria o socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Erro ao criar o socket");
        return 1;
    }

    // Cria a estrutura para definir o tempo de espera para receber pacotes.
    struct timeval tv = {CLIENT_TIMEOUT_SEC, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Declara a estrutura para armazenar o endereço do servidor.
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;    // Define o tipo de endereço (IPv4)
    server_addr.sin_port = htons(PORT);  // Define a porta do servidor
    inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

    // Monta o pacote de login que será enviado ao servidor.
    // O primeiro byte é o tipo do pacote (LOGIN_REQUEST) e o restante é o
    // nome do cliente.
    std::vector<char> login_packet;
    login_packet.push_back(LOGIN_REQUEST);
    login_packet.insert(login_packet.end(), client_name.begin(),
                        client_name.end());

    // Envia o pacote de login para o servidor via UDP.
    sendto(sock, login_packet.data(), login_packet.size(), 0,
           (sockaddr*)&server_addr, sizeof(server_addr));

    std::cout << "Tentando conectar como '" << client_name << "'..."
              << std::endl;

    // Flag para controlar a execução das threads.
    running = true;

    // Inicia as threads de envio, recebimento e reprodução de áudio.
    // Cada thread é responsável por uma parte do processo de comunicação:
    std::thread receiver(receive_thread_func, sock);
    std::thread sender(send_thread_func, sock, server_addr);
    std::thread player(playback_thread_func);

    // Aguardando o usuário pressionar Enter para encerrar o programa.
    std::cout << "\n*** Pressione Enter para sair. ***\n";
    std::cin.get();

    std::cout << "Encerrando as threads..." << std::endl;

    // Com a flag em false os loops das threads vão terminar.
    running = false;

    // Caso a thread de playback esteja esperando por novos pacotes,
    // notifica-a para que ela possa sair do loop.
    cv.notify_all();

    // Aguarda as threads terminarem.
    // A função join bloqueia a thread principal até que a thread especificada
    // termine sua execução.
    sender.join();
    receiver.join();
    player.join();

    // Fecha o socket
    close(sock);

    std::cout << "Programa encerrado." << std::endl;

    return 0;
}