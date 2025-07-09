#pragma once

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "audio.h"

// Interruptor geral de todas as threads
extern std::atomic<bool> running;

// Variável que armazena pacotes de áudio recebidos.
// Cada pacote é um vetor de caracteres, representando um bloco de áudio.
// A thread de recebimento coloca pacotes aqui, e a thread de playback
// retira para tocar.
extern std::queue<std::vector<char>> jitter_buffer;

// A variável mutex bloqueia o acesso ao jitter_buffer enquanto uma
// thread está adicionando ou removendo pacotes, garantindo que apenas uma
// thread possa modificar a fila por vez, evitando erros como corrupção de
// dados ou comportamento inesperado.
extern std::mutex jitter_buffer_mutex;

// A thread de recebimento toca a campainha quando um novo pacote chega,
// acordando a thread de playback para que ela possa processar o pacote.
// Evita que a thread de playback fique acordada o tempo todo, economizando
// recursos do sistema.
extern std::condition_variable cv;

// Função para descobrir o IP do servidor na rede local.
std::string discover_server_on_network();

// Declaração da função para suprimir erros, necessário para os erros  que o
// PortAudio lança ao tentar encontrar os dispositivos de áudio
void suppress_alsa_errors(bool suppress);

// Função responsável por receber pacotes de áudio do servidor e
// colocá-los no jitter buffer, que é uma fila de pacotes a serem
// reproduzidos. Ela recebe o socket como parâmetro.
void receive_thread_func(int sock);

// Função responsável por capturar áudio do microfone e enviá-lo para o
// servidor, recebe o socket e o endereço do servidor como parâmetros.
void send_thread_func(int sock, const sockaddr_in& server_addr);

// Função responsável por tocar o áudio recebido do jitter buffer.
// Ela não recebe parâmetros, pois acessa o jitter buffer e o motor de áudio
// diretamente.
void playback_thread_func();