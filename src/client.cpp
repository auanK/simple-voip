#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "client_handler.h"
#include "common.h"

// Instância global do motor de áudio (definida no client_utils.cpp)
extern AudioHandler audio_handler;

// Função que aguarda o usuário pressionar Enter para encerrar o programa.
void wait_for_enter() {
    std::cin.get();
    running = false;
}

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

#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return 1;
    }
#endif

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
#ifdef __linux__
    suppress_alsa_errors(true);
#endif
    bool audio_ok = audio_handler.init();
#ifdef __linux__
    suppress_alsa_errors(false);
#endif

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
#ifdef _WIN32
    DWORD timeout = CLIENT_TIMEOUT_SEC * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout,
               sizeof(timeout));
#else
    struct timeval tv = {CLIENT_TIMEOUT_SEC, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

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

    // Cria a promessa e o futuro para sincronizar a conexão.
    // A promessa é usada para notificar a thread principal quando a conexão
    // for estabelecida, enquanto o futuro é usado para aguardar essa
    // notificação na thread principal.
    std::promise<void> connection_promise;
    std::future<void> connection_future = connection_promise.get_future();

    // Inicia primeiro a thread de recebimento para que ela possa processar a
    // resposta do servidor antes de enviar pacotes de áudio ou reproduzir
    // áudio.
    std::thread receiver(receive_thread_func, sock,
                         std::move(connection_promise));

    // Declara as threads de envio e reprodução.
    std::thread sender, player;

    // Declara a thread de input para aguardar o usuário pressionar Enter
    std::thread input_thread;

    try {
        // Esperando a thread de recebimento confirmar a conexão com o servidor.
        std::cout << "Aguardando confirmação do servidor..." << std::endl;
        connection_future.get();

        // Inicia a thread de input
        input_thread = std::thread(wait_for_enter);

        // Inicia a thread de envio e a thread de reprodução de áudio.
        sender = std::thread(send_thread_func, sock, server_addr);
        player = std::thread(playback_thread_func);

        // Aguarda a thread de input terminar.
        input_thread.join();

        sendto(sock, "", 1, 0, (sockaddr*)&server_addr,
               sizeof(server_addr)); 
                                       

    } catch (const std::exception& e) {
        // Se ocorrer um erro ao estabelecer a conexão, exibe a mensagem de erro
        // e encerra as threads.
        std::cerr << "\n[ERRO] Não foi possível estabelecer a conexão."
                  << std::endl;
        running = false;
        receiver.join();

        // Limpa o socket
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return 1;
    }

    // Informa para o servidor que o cliente vai se desconectar.
    char logout_packet = LOGOUT_NOTICE;
    sendto(sock, &logout_packet, sizeof(logout_packet), 0,
           (sockaddr*)&server_addr, sizeof(server_addr));

    // Caso a thread de playback esteja esperando por novos pacotes,
    // notifica-a para que ela possa sair do loop.
    jitter_buffer_cond.notify_all();

    // Aguarda as threads terminarem.
    // A função join bloqueia a thread principal até que a thread especificada
    // termine sua execução.
    sender.join();
    receiver.join();
    player.join();

// Fecha o socket
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    std::cout << "Programa encerrado." << std::endl;

    return 0;
}