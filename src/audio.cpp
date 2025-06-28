#include "audio.h"

#include <iostream>

#include "common.h"

// Função auxiliar para converter o SAMPLE_SIZE para o formato do PortAudio.
// Retorna o tipo de dado correto para o PortAudio com base no tamanho da
// amostra em bytes.
PaSampleFormat getPaSampleFormat() {
    switch (SAMPLE_SIZE) {
        case 1:
            return paInt8;
        case 2:
            return paInt16;
        case 4:
            return paInt32;
        default:
            return paCustomFormat;
    }
}

// Construtor da classe AudioHandler
AudioHandler::AudioHandler() : inputStream(nullptr), outputStream(nullptr) {}

// Destrutor da classe AudioHandler
AudioHandler::~AudioHandler() { terminate(); }

// Inicializa a biblioteca PortAudio
bool AudioHandler::init() {
    // Tenta inicializar a biblioteca PortAudio
    // A função Pa_Initialize() prepara a biblioteca para ser usada.
    PaError err = Pa_Initialize();

    // Verifica se houve erro na inicialização
    if (err != paNoError) {
        std::cerr << "Erro de PortAudio: Pa_Initialize() falhou: "
                  << Pa_GetErrorText(err) << std::endl;
        return false;  // Erro ao inicializar
    }
    return true;  // Sucesso ao inicializar
}

// Desaloca os recursos utilizados pela biblioteca PortAudio
void AudioHandler::terminate() {
    // Para o fluxo de captura de áudio, se estiver ativo
    if (inputStream) {
        Pa_StopStream(inputStream);
        Pa_CloseStream(inputStream);
        inputStream = nullptr;
    }

    // Para o fluxo de reprodução de áudio, se estiver ativo
    if (outputStream) {
        Pa_StopStream(outputStream);
        Pa_CloseStream(outputStream);
        outputStream = nullptr;
    }

    // Finaliza a biblioteca PortAudio
    Pa_Terminate();
}

// Inicia o fluxo de captura de áudio
void AudioHandler::startCapture() {
    // O tipo de dado 'PaStreamParameters' é utilizado para definir os
    // parâmetros de entrada e saída de áudio na biblioteca PortAudio.
    PaStreamParameters inputParameters;

    // O dispositivo de entrada é o microfone padrão
    inputParameters.device = Pa_GetDefaultInputDevice();

    // Caso nenhum dispositivo de entrada padrão seja encontrado, exibe um erro
    if (inputParameters.device == paNoDevice) {
        std::cerr << "Erro: Nenhum dispositivo de entrada padrão encontrado."
                  << std::endl;
        return;  // Encerra com o erro
    }

    // Preenchemos o número de canais e a profundidade de bits com os valores do
    // arquivo common.h
    inputParameters.channelCount = NUM_CHANNELS;
    inputParameters.sampleFormat = getPaSampleFormat();

    if (inputParameters.sampleFormat == paCustomFormat) {
        std::cerr << "Erro: Formato de amostra não suportado." << std::endl;
        return;  // Encerra com o erro
    }

    inputParameters.suggestedLatency =
        Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    // Tenta abrir o fluxo de captura de áudio
    PaError err =
        Pa_OpenStream(&inputStream, &inputParameters, NULL, SAMPLE_RATE,
                      FRAMES_PER_BUFFER, paClipOff, NULL, NULL);

    // Verifica se houve erro ao abrir o fluxo
    if (err != paNoError) {
        std::cerr << "Erro de PortAudio: Pa_OpenStream() falhou: "
                  << Pa_GetErrorText(err) << std::endl;
        return;  // Encerra com o erro
    }

    // Se o stream foi aberto com sucesso, o Pa_StartStream o ativa
    Pa_StartStream(inputStream);  // Microfone começa a capturar áudio
}

// Inicia o fluxo de reprodução de áudio
void AudioHandler::startPlayback() {
    // O tipo de dado 'PaStreamParameters' é utilizado para definir os
    // parâmetros de entrada e saída de áudio na biblioteca PortAudio.
    PaStreamParameters outputParameters;

    // O dispositivo de saída é o alto-falante padrão
    outputParameters.device = Pa_GetDefaultOutputDevice();

    // Caso nenhum dispositivo de saída padrão seja encontrado, exibe um erro
    if (outputParameters.device == paNoDevice) {
        std::cerr << "Erro: Nenhum dispositivo de saída padrão encontrado."
                  << std::endl;
        return;  // Encerra com o erro
    }

    // Preenchemos o número de canais e a profundidade de bits com os valores do
    // arquivo common.h
    outputParameters.channelCount = NUM_CHANNELS;
    outputParameters.sampleFormat = getPaSampleFormat();

    if (outputParameters.sampleFormat == paCustomFormat) {
        std::cerr << "Erro: Formato de amostra não suportado." << std::endl;
        return;  // Encerra com o erro
    }

    outputParameters.suggestedLatency =
        Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    // Tenta abrir o fluxo de reprodução de áudio
    PaError err =
        Pa_OpenStream(&outputStream, NULL, &outputParameters, SAMPLE_RATE,
                      FRAMES_PER_BUFFER, paClipOff, NULL, NULL);

    // Verifica se houve erro ao abrir o fluxo
    if (err != paNoError) {
        std::cerr << "Erro de PortAudio: Pa_OpenStream() falhou: "
                  << Pa_GetErrorText(err) << std::endl;

        return;  // Encerra com o erro
    }

    // Se o stream foi aberto com sucesso, o Pa_StartStream o ativa
    Pa_StartStream(outputStream);  // Alto-falante começa a reproduzir áudio
}

// Para o fluxo de captura de áudio
void AudioHandler::stopCapture() {
    if (inputStream) Pa_StopStream(inputStream);
}

// Para o fluxo de reprodução de áudio
void AudioHandler::stopPlayback() {
    if (outputStream) Pa_StopStream(outputStream);
}

// Lê um bloco de áudio do microfone e armazena no 'buffer' fornecido
int AudioHandler::read(char* buffer) {
    // Retorna erro se o stream não estiver ativo
    if (!inputStream) return paBadStreamPtr;

    // A função Pa_ReadStream coleta os dados do hardware de entrada
    // e os armazena no buffer fornecido.
    return Pa_ReadStream(inputStream, buffer, FRAMES_PER_BUFFER);
}

// Pega um bloco de áudio armazenado no 'buffer' e o envia para a saída de áudio
int AudioHandler::write(const char* buffer) {
    // Retorna erro se o stream não estiver ativo
    if (!outputStream) return paBadStreamPtr;
    // A função Pa_WriteStream envia os dados do buffer para o hardware de saída
    return Pa_WriteStream(outputStream, buffer, FRAMES_PER_BUFFER);
}