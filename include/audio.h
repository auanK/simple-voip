#pragma once

#include <portaudio.h>

// Classe que encapsula o tratamento de áudio usando a biblioteca PortAudio
// Utilizada para facilitar a captura e reprodução de áudio no projeto.
class AudioHandler {
   private:
    // O tipo de dado 'PaStream' é utilizado para representar um fluxo de áudio
    // na biblioteca PortAudio. Ele é usado tanto para capturar áudio do
    // microfone quanto para reproduzir áudio nos alto-falantes.

    // Ponteiro para o fluxo de captura de áudio (microfone)
    PaStream* inputStream;

    // Ponteiro para o fluxo de reprodução de áudio (alto-falantes)
    PaStream* outputStream;

   public:
    // Construtor da classe
    AudioHandler();

    // Destrutor da classe
    ~AudioHandler();

    // Prepara o biblioteca PortAudio para ser usada
    // Retorna true se a inicialização for bem-sucedida, false caso contrário
    bool init();

    // Desaloca os recursos utilizados pela biblioteca PortAudio
    void terminate();

    // Abre e inicia o fluxo de captura de áudio
    void startCapture();
    
    // Abre e inicia o fluxo de reprodução de áudio
    void startPlayback();
    
    // Para o fluxo de captura de áudio
    void stopCapture();

    // Para o fluxo de reprodução
    void stopPlayback();

    // Lê um bloco de áudio do microfone e armazena no 'buffer' fornecido.
    // O 'buffer' é um ponteiro para um array de caracteres onde os dados de
    // áudio serão armazenados.
    int read(char* buffer);

    // Pega um bloco de áudio armazenado no 'buffer' e o envia para a saída de
    // áudio.
    int write(const char* buffer);
};