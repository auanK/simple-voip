# Simple-VoIP

Trabalho Final da cadeira de Redes de Computadores do curso de Ciência da Computação da UFC - Campus Quixadá, lecionada pelo professor Arthur de Castro Callado no semestre 2025.1. Feito pelo aluno [Kauan Pablo](https://github.com/auanK).

## Descrição do Projeto

Este projeto consiste no desenvolvimento de uma aplicação VoIP em C++. A aplicação permite comunicação por áudio em tempo real entre dois usuários através de uma arquitetura cliente-servidor, utilizando o protocolo UDP para garantir baixa latência, essencial para conversas fluidas.

Os principais destaques do projeto incluem:

- **Áudio de Baixa Latência:** O projeto utiliza a biblioteca `PortAudio` com uma camada de abstração para a manipulação de áudio, configurada com uma qualidade considerada alta e um processamento em pacotes de 20ms, o padrão em comunicação de tempo real.

- **Multiplataforma:** O código foi cuidadosamente portado para compilar e funcionar nativamente tanto no **Linux** quanto no **Windows**.

- **Multithread:** O cliente utiliza uma arquitetura concorrente com `std::thread` para rodar simultaneamente a captura/envio de áudio, o recebimento e a reprodução, garantindo que a aplicação se mantenha sempre responsiva.

- **Broadcast:** Inclui uma funcionalidade de descoberta automática (broadcast), que permite em redes locais o cliente encontrar o servidor sem a necessidade de informar manualmente o endereço IP.

### Dependências

É necessário ter o C++ (versão 17 ou superior) instalado, além da biblioteca PortAudio instalada.

### Compilando no Linux

```bash
g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude src/server.cpp src/server_handler.cpp -o servidor
g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude src/client.cpp src/client_handler.cpp src/audio.cpp -o cliente -lportaudio -lpthread
```

### Compilando no Windows (com libs inclusas no arquivo compilado)

```bash
g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude src/server.cpp src/server_handler.cpp -o servidor.exe -lws2_32 -static
g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude src/client.cpp src/client_handler.cpp src/audio.cpp -o cliente.exe -lportaudio -lpthread -lws2_32 -static -lwinmm -lole32 -lsetupapi
```

## Documentação

A seguir está algumas informações do projeto e da arquitetura, além do principal fluxo de funcionamento da aplicação.

### Conversão do som analógico para digital

O som é uma onda que se propaga pelo ar, de forma analógica, enquanto os computadores armazenam dados digitalmente. Portanto, para armazenar áudio em um computador, é necessário converter as ondas sonoras analógicas em um formato digital. Como um equilibrio entre a qualidade e a latência do projeto, utilizei as seguintes valores para cada parte do áudio digital:

### Configurações de Áudio do Projeto

- `SAMPLE_RATE:` A taxa de amostragem indica quantas amostras de áudio são capturadas por segundo (comparável ao FPS em um vídeo). Quanto maior essa taxa, mais detalhes são registrados no áudio, melhorando a qualidade do som. 48000 Hz é um valor comum para áudio de alta qualidade, usado em gravações profissionais, transmissões e comumente utilizado no Discord. O ser humano ouve até cerca de 20000 Hz, e pelo Teorema de Nyquist-Shannon, para representar com precisão um sinal analógico em formato digital, a taxa de amostragem deve ser pelo menos o dobro da frequência mais alta do sinal, normalmente é usado valores maiores para evitar distorções principalmente em edições.

- `FRAMES_PER_BUFFER:` Número de amostras de áudio que são processadas de uma só vez. Um buffer maior significa que o programa precisa esperar mais tempo para processar o áudio, aumentando a latência mas reduzindo o uso da CPU, já um buffer menor significa que o programa processa o áudio mais rapidamente, mas exige mais da CPU, pois precisa lidar com pacotes de áudio com mais frequência. 960 é um valor padrão para aplicações de áudio em tempo real como o Discord, pois o atraso é de aproximadamente 20 ms (960 amostras / 48000 Hz) e é um bom valor entre latência e uso da CPU.

- `SAMPLE_SIZE:` Profundidade de bits em cada amostra de áudio, em bytes. Pode ser comparada à "profundidade de cor" ou "contraste" em imagens, mas para áudio, onde quanto maior esse valor maior a diferença entre o som mais baixo capturado e o mais alto, aumentando a riqueza do áudio. Com 16 bits, o utilizado em CDs de áudio, cada amostra pode ter 65536 valores possíveis, o que dá em torno de 96 dB de faixa dinâmica.

- `NUM_CHANNELS:` O número de canais de áudio (como mono ou estéreo). Para uma chamada de voz mono é suficiente, em áudio estéreo o consumo o tamanho do pacote dobraria para conter todas as informações. (O projeto só suporta mono áudio no momento).

- `AUDIO_BUFFER_SIZE:` O tamanho total de um pacote de áudio, em bytes. Importante para definir o tamanho do buffer que será utilizado para capturar e processar áudio. Calculado com `FRAMES_PER_BUFFER × SAMPLE_SIZE × NUM_CHANNELS`, o que no projeto da exatamente 1920 bytes de dados.

### Configurações de rede

Para aplicações em tempo real onde o mínimo de latência é desejado, como essa aplicação VoIP, utilizamos o protocolo UDP que apesar de não ter uma série de benefícios que o TCP tem como sessão, ou garantia de envio, é um protocolo mais simples e rápido, em aplicações desse tipo é mais desejável perder algum pacote de áudio no caminho e continuar a transmissão do que interromper a execução para aguardar o reenvio do pacote.

Algumas configurações que definimos:

- `PORT: 12345` Porta padrão para o servidor de áudio.
- `CLIENT_TIMEOUT_SEC: 15` Tempo máximo de inatividade de um cliente antes da desconexão (segundos).
- `MAX_NAME_LENGTH: 50` Tamanho máximo do nome de um cliente.
- `DISCOVERY_TIMEOUT_SEC: 10` Tempo de espera ao procurar o servidor na rede local (broadcast).
- O servidor foi configurado para utilizar qualquer endereço IPv4 disponível na máquina.

### Protocolo de Comunicação

Como o protocolo UDP não guarda sessão de conexão, dentro dos pacotes de dados (dado útil carregado pelo protocolo IPv4) foi adicionado um byte no início do pacote para ajudar o servidor e o cliente lidarem com diferentes situações. Foi utilizada uma estrutura Enum do tipo `PacketType` para melhorar a legibilidade.

| Identificador        | Valor Hex | Descrição                                       |
| -------------------- | --------- | ----------------------------------------------- |
| `LOGIN_REQUEST`      | `0x01`    | Cliente envia nome para conectar.               |
| `AUDIO_DATA`         | `0x02`    | Pacote contendo dados de áudio.                 |
| `SERVER_MESSAGE`     | `0x03`    | Mensagem de sistema enviada pelo servidor.      |
| `LOGIN_OK`           | `0x04`    | Confirmação de login pelo servidor.             |
| `SERVER_FULL`        | `0x05`    | Informa que o servidor atingiu o número máximo. |
| `DISCOVERY_REQUEST`  | `0x06`    | Cliente envia broadcast procurando o servidor.  |
| `DISCOVERY_RESPONSE` | `0x07`    | Resposta do servidor ao pedido de descoberta.   |
| `KEEPALIVE_PONG`     | `0x08`    | Ping/pong para manter a conexão ativa.          |
| `LOGOUT_NOTICE`      | `0x09`    | Cliente informa que está desconectando.         |

### Arquitetura da Aplicação

O projeto é dividido em duas partes principais, um servidor de retransmissão e um cliente multithread, seus principais fluxos de funcionamento são:

### Fluxo do Servidor

O servidor gerencia até dois cientes simultâneos, armazenando suas informações de nome, endereço e tempo de última atividade.

Ele atua como um retransmissor de pacotes de áudio: Ao receber um pacote de áudio de um cliente ele imediatamente repassa ao outro. Ele também envia mensagens do sistema para todos os usuários conectados, informando entradas e saídas dos participantes além de enviar um `KEEPALIVE_PONG` para manter uma conexão ativa com um cliente sozinho na chamada.

Como a função `recvfrom()` é uma chamada bloqueante, loop principal do servidor utiliza o `select()` para aguardar pacotes com um pequeno timeout caso não haja nenhuma atividade por partes dos clientes, permitindo verificar periodicamente se os clientes ainda estão ativos e os desconectar caso estejam inativos por mais de `CLIENT_TIMEOUT_SEC`, o outro cliente na chamada (caso exista) é notificado.

### Fluxo do Cliente

Ao iniciar o cliente envia um pacote com o cabeçalho `LOGIN_REQUEST` e aguarda o servidor responder para começar seu fluxo de execução.

Ele utiliza os seguintes tipos de variáveis globais para sincronizar as threads em um único fluxo de execução:

`jitter_buffer:` Variável que armazena pacotes de áudio recebidos, cada pacote é um vetor de caracteres, representando um bloco de áudio. A thread de recebimento coloca pacotes aqui, e a thread de playback retira para tocar.

`jitter_buffer_mutex:` A variável mutex bloqueia o acesso ao jitter_buffer enquanto uma thread está adicionando ou removendo pacotes, garantindo que apenas uma thread possa modificar a fila por vez, evitando a famosa condição de corrida que pode causar erros inesperados ou corrupção de dados.

`jitter_buffer_cond:` A variável de condição é usada para notificar a thread de reprodução de áudio quando novos pacotes estão disponíveis no jitter_buffer.

O cliente é multithread e realiza 3 tarefas principais de forma paralela.

1. **Envio de Áudio:** a funcão `send_thread_func()` captura o áudio do microfone com a PortAudio e envia para o servidor via UDP.
2. **Recepção de Áudio:** a função `receive_thread_func()` recebe pacotes do servidor e armazena no `jitter_buffer`
3. **Reprodução de Áudio:** a função `playback_thread_func()` lê os pacotes do `jitter_buffer` e reproduz nos alto-falantes, mantendo o áudio sincronizado.

Se o IP do servidor não for especificado, o cliente faz uma tentativa de descoberta automática por broadcast na rede local (255.255.255.255), enviando um pacote `DISCOVERY_REQUEST` e aguardando uma resposta do tipo `DISCOVERY_RESPONSE`.

## Observações

### Qualidade de Áudio e Codecs

A aplicação transmite áudio **PCM não comprimido**, o que garante máxima fidelidade ao som capturado, mas não é eficiente em termos de transmissão da rede.
Aplicações VoIP de alta qualidade, como o Discord ou o Skype, utilizam codecs de áudio avançados como o **Opus** que reduz drasticamente o tamanho dos pacotes de áudio com uma perda de qualidade imperceptível. 

Por exemplo nesse projeto, o tamanho total de um pacote é de `1 (cabeçalho personalizado) + 1920 (áudio) + 8 (UDP) + 20 (IP padrão) = 1949 bytes`, o que já ultrapassa o MTU padrão do IPv4 de 1500 bytes, sendo necessário fragmentação o que adiciona complexidade e a probabilidade de perda de pacotes.

Além disso para uma experiência realmente polida, seriam necessários outros componentes como:

- **Supressão de Ruído:** Para remover ruídos de fundo indesejados.
- **Cancelamento de Eco Acústico:** Para impedir que o som que sai dos seus alto-falantes seja capturado pelo seu microfone e enviado de volta.

Essas implementações são praticamente obrigatórias em aplicações em um nível comercial.

### Criptografia

A criptografia da comunicação é um requisito em qualquer aplicação real. Para o escopo do projeto puramente acadêmico onde o foco foi voltado principalmente para a parte da conversão de áudio analógico para digital e sua transmissão em tempo real, implementar uma criptografia segura nesse sistema UDP aumentaria consideravelmente a complexidade e arquitetura do projeto , então a decisão foi de não foi implementar.

## Conclusão

O projeto consolidou muito meus conhecimentos principalmente de C++ moderno, e foi minha primeira experiência na construção tanto de uma aplicação de rede, quanto multiplataforma, quanto com uso de threads, acredito que levarei a experiência para futuros desafios e projetos.
