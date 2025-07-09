# Simple-VoIP
### Compilando no Linux
```bash
g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude src/server.cpp src/server_handler.cpp -o servidor
g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude src/client.cpp src/client_handler.cpp src/audio.cpp -o cliente -lportaudio -lpthread
```

### Compilando no Windows
```bash
g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude src/server.cpp src/server_handler.cpp -o servidor.exe -lws2_32 -static
g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude src/client.cpp src/client_handler.cpp src/audio.cpp -o cliente.exe -lportaudio -lpthread -lws2_32 -static -lwinmm -lole32 -lsetupapi
```