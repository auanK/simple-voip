#pragma once

// Taxa de amostragem (sample rate), em Hz
// Indica quantas amostras de áudio são capturadas por segundo (Como se fosse o
// FPS de um vídeo). Quanto maior essa taxa, mais detalhes são registrados no
// áudio, melhorando a qualidade do som. 48 kHz é um valor comum para áudio de
// alta qualidade, usado em gravações profissionais, transmissões e utilizado no
// Discord.
constexpr int SAMPLE_RATE = 48000;

// Número de amostras de áudio que são processadas de uma só vez.
// Um buffer maior significa que o programa precisa esperar mais tempo para
// processar o áudio, aumentando a latência mas reduzindo o uso da CPU, já um
// buffer menor significa que o programa processa o áudio mais rapidamente, mas
// exige mais da CPU, pois precisa lidar com pacotes de áudio com mais
// frequência. 960 é um valor padrão para aplicações de áudio em tempo real como
// o Discord, pois o atraso é de aproximadamente 20 ms (960 amostras / 48000 Hz)
// e é um bom valor entre latência e uso da CPU.
constexpr int FRAMES_PER_BUFFER = 960;

// Tamanho em bytes de cada amostra de áudio, o que determina a profundidade de
// bits. É basicamente como a "profundidade de cor" ou "contraste" em imagens,
// mas para áudio, do som mais suave ao mais alto.
// Com 16 bits, cada amostra pode ter 65536 valores possíveis, o que dá em torno
// de 96 dB de faixa dinâmica, o que é como se fosse a diferença entre o coração
// batendo e um avião decolando.
constexpr int SAMPLE_SIZE = 2;

// O número de canais de áudio (como mono ou estéreo).
// Para uma chamada de voz mono é suficiente um único canal, reduzindo
// o consumo de rede e processamento pela metade em comparação com
// um áudio estéreo.
constexpr int NUM_CHANNELS = 1;

// Tamanho total de um pacote de dados do áudio, em bytes.
// Importante para definir o tamanho do buffer de áudio que será
// utilizado para capturar e processar o áudio.
constexpr int AUDIO_BUFFER_SIZE =
    FRAMES_PER_BUFFER * NUM_CHANNELS * SAMPLE_SIZE;
