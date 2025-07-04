# simple-voip

g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude src/server.cpp src/server_handler.cpp -o servidor

g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude src/client.cpp src/client_handler.cpp src/audio.cpp -o cliente -lportaudio -lpthread