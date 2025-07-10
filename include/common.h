#pragma once

// Taxa de amostragem, em Hertz (Hz), do áudio.
constexpr int SAMPLE_RATE = 48000;

// Número de amostras de áudio por buffer.
constexpr int FRAMES_PER_BUFFER = 960;

// Profundidade de bits de cada amostra de áudio.
constexpr int SAMPLE_SIZE = 2;

// Número de canais de áudio. (1 para mono, 2 para estéreo).
constexpr int NUM_CHANNELS = 1;

// Tamanho total de um pacote de áudio em bytes.
constexpr int AUDIO_BUFFER_SIZE =
    FRAMES_PER_BUFFER * NUM_CHANNELS * SAMPLE_SIZE;

// Define a porta padrão para o servidor de áudio
constexpr int PORT = 12345;

// Tempo máximo de espera para receber pacotes do servidor
constexpr int CLIENT_TIMEOUT_SEC = 1;

// Define o tamanho máximo para o nome do cliente
constexpr int MAX_NAME_LENGTH = 50;

// Tempo de espera para encontrar o servidor na rede local
constexpr int DISCOVERY_TIMEOUT_SEC = 10;

// Define um protocolo simples com um cabeçalho de 1 byte
enum PacketType : char {
    LOGIN_REQUEST = 0x01,       // Cliente envia nome para conectar
    AUDIO_DATA = 0x02,          // Pacote de áudio
    SERVER_MESSAGE = 0x03,      // Servidor envia notificação
    LOGIN_OK = 0x04,            // Servidor confirma conexão
    SERVER_FULL = 0x05,         // Servidor informa que está cheio
    DISCOVERY_REQUEST = 0x06,   // Cliente procura por um servidor
    DISCOVERY_RESPONSE = 0x07,  // Servidor responde que está ativo
    KEEPALIVE_PONG = 0x08,      // Ping para manter a conexão ativa
    LOGOUT_NOTICE = 0x09,       // Cliente avisa desconexão
};