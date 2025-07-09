#pragma once

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "audio.h"

// Interruptor geral de todas as threads
extern std::atomic<bool> running;

// Armazena os pacotes de áudio recebidos do servidor.
extern std::queue<std::vector<char>> jitter_buffer;

// A variável mutex bloqueia o acesso ao jitter_buffer enquanto uma
// thread está adicionando ou removendo pacotes.
extern std::mutex jitter_buffer_mutex;

// A variável condition_variable é usada para notificar a thread de
// reprodução de áudio quando novos pacotes estão disponíveis no jitter_buffer.
extern std::condition_variable jitter_buffer_cond;

// Função para descobrir o IP do servidor na rede local.
std::string discover_server_on_network();

// Declaração da função para suprimir erros, necessário para os erros  que o
// PortAudio lança ao tentar encontrar os dispositivos de áudio
void suppress_alsa_errors(bool suppress);

// Função responsável por receber pacotes de áudio do servidor e
// colocá-los no jitter buffer, que é uma fila de pacotes a serem
// reproduzidos. Ela recebe o socket como parâmetro.
void receive_thread_func(int sock, std::promise<void> connection_promise);

// Função responsável por capturar áudio do microfone e enviá-lo para o
// servidor, recebe o socket e o endereço do servidor como parâmetros.
void send_thread_func(int sock, const sockaddr_in& server_addr);

// Função responsável por tocar o áudio recebido do jitter buffer.
// Ela não recebe parâmetros, pois acessa o jitter buffer e o motor de áudio
// diretamente.
void playback_thread_func();