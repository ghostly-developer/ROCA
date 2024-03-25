#include <iostream>
#include <string>
#include <WS2tcpip.h>
#include <thread>
#include <mutex>
#include <map> 
#include <fstream>
#include <sstream>  
#include <vector>
#include <string>
#include <windows.h>
#include <algorithm>
#include <cstring> 

#pragma comment(lib, "ws2_32.lib")

std::mutex clientMutex;
std::map<SOCKET, std::string> clientNames; // Correct declaration of std::map
std::vector<SOCKET> clientSockets;

const std::string configFileName = "server_config.txt";
bool isAltEPressed() {
    return (GetAsyncKeyState(VK_MENU) & 0x8000) && (GetAsyncKeyState(0x45) & 0x8000);
}

struct Channel {
    std::string name;
    std::vector<SOCKET> clients;
};

struct ServerConfig {
    std::string name;
    std::string ipAddress;
    int port;
    std::vector<Channel> channels;
};

void broadcastMessage(const std::string& message, SOCKET excludeSock = INVALID_SOCKET) {
    std::lock_guard<std::mutex> lock(clientMutex);
    for (SOCKET sock : clientSockets) {
        if (sock != excludeSock) {  // Don't send the message back to the sender
            send(sock, message.c_str(), message.length(), 0);
        }
    }
}

std::string getLocalIPAddress() {
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    struct addrinfo hints = {}, * info;
    hints.ai_family = AF_INET; // AF_INET indicates IPv4
    hints.ai_socktype = SOCK_STREAM;

    std::string ipAddress;
    if (getaddrinfo(hostname, nullptr, &hints, &info) == 0) {
        for (auto p = info; p != nullptr; p = p->ai_next) {
            sockaddr_in* ipv4 = (sockaddr_in*)p->ai_addr;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ipv4->sin_addr, ip, sizeof(ip));
            ipAddress = ip;
            break;
        }
        freeaddrinfo(info);
    }
    return ipAddress;
}

void deleteServerConfig(int index, std::vector<ServerConfig>& configs) {
    if (index >= 0 && index < configs.size()) {
        configs.erase(configs.begin() + index);
        std::cout << "Configuration deleted." << std::endl;
    }
    else {
        std::cerr << "Invalid configuration index." << std::endl;
    }
}

void editServerConfig(int index, std::vector<ServerConfig>& configs) {
    if (index >= 0 && index < configs.size()) {
        std::string name, ip;
        int port;

        std::cout << "Enter new name (current: " << configs[index].name << "): ";
        std::getline(std::cin, name);
        std::cout << "Enter new IP address (current: " << configs[index].ipAddress << "): ";
        std::getline(std::cin, ip);
        std::cout << "Enter new port (current: " << configs[index].port << "): ";
        std::cin >> port;
        std::cin.ignore();

        std::cout << "Current channels: ";
        for (const auto& channel : configs[index].channels) {
            std::cout << channel.name << " ";
        }
        std::cout << "Enter new list of channels (comma-separated, no spaces): ";
        std::string channelsInput;
        std::getline(std::cin, channelsInput);
        std::istringstream iss(channelsInput);
        configs[index].channels.clear();  // Clear existing channels
        std::string channelName;
        while (getline(iss, channelName, ',')) {
            Channel newChannel;
            newChannel.name = channelName;
            configs[index].channels.push_back(newChannel);  // Add new channel
        }

        // Update the server configuration
        configs[index].name = name;
        configs[index].ipAddress = ip;
        configs[index].port = port;
    }
    else {
        std::cerr << "Invalid configuration index." << std::endl;
    }
}

void saveServerConfig(const ServerConfig& config) {
    std::ofstream outFile("server_configs.txt", std::ios::app);  // Append mode
    if (outFile.is_open()) {
        outFile << config.name << "," << config.ipAddress << "," << config.port;
        for (const auto& channel : config.channels) {
            outFile << "," << channel.name;  // Save channels
        }
        outFile << "\n";
        outFile.close();
    }
    else {
        std::cerr << "Unable to open file for saving." << std::endl;
    }
}



void saveAllServerConfigs(const std::vector<ServerConfig>& configs) {
    std::ofstream outFile("server_config.txt");
    if (outFile.is_open()) {
        for (const auto& config : configs) {
            outFile << config.name << "," << config.ipAddress << "," << config.port << std::endl;
        }
        outFile.close();
    }
    else {
        std::cerr << "Unable to open file for writing." << std::endl;
    }
}

std::vector<ServerConfig> loadServerConfigs() {
    std::vector<ServerConfig> configs;
    std::ifstream inFile(configFileName);
    std::string line;

    if (inFile.is_open()) {
        while (getline(inFile, line)) {
            std::istringstream iss(line);
            ServerConfig config;
            std::getline(iss, config.name, ',');
            std::getline(iss, config.ipAddress, ',');
            iss >> config.port;

            // Assume the rest of the line consists of channel names
            std::string channelName;
            while (getline(iss, channelName, ',')) {
                if (!channelName.empty()) {
                    Channel channel;
                    channel.name = channelName;
                    config.channels.push_back(channel);
                }
            }

            configs.push_back(config);
        }
        inFile.close();
    }
    return configs;
}

std::vector<ServerConfig> listServerConfigs(bool edit) {
    auto configs = loadServerConfigs();
    std::cout << "Saved server configurations:" << std::endl;
    for (int i = 0; i < configs.size(); ++i) {
        std::cout << i + 1 << ". Name: " << configs[i].name
            << ", IP: " << configs[i].ipAddress
            << ", Port: " << configs[i].port << std::endl;
    }

    if (edit == true) {
        std::cout << "Enter 'd' to delete, 'e' to edit a configuration, or 'b' to go back: ";
        std::string action;
        std::cin >> action;
        std::cin.ignore();

        if (action == "d" || action == "e") {
            int index;
            std::cout << "Enter the number of the configuration: ";
            std::cin >> index;
            std::cin.ignore();
            index--;  // Adjust for zero-based index

            if (action == "d") {
                deleteServerConfig(index, configs);
            }
            else if (action == "e") {
                editServerConfig(index, configs);
            }

            saveAllServerConfigs(configs); // Save changes back to the file
        }
        return configs;
    }
    return configs;
}


void handle_connection(SOCKET sock, ServerConfig& serverConfig) {
    // Placeholder for receiving the username
    char nameBuffer[1024];
    ZeroMemory(nameBuffer, sizeof(nameBuffer));
    int nameBytesReceived = recv(sock, nameBuffer, sizeof(nameBuffer), 0);
    std::string clientName(nameBuffer, nameBytesReceived);

    // Join the first channel by default
    Channel* currentChannel = &serverConfig.channels.front();
    currentChannel->clients.push_back(sock);

    char buffer[1024];
    while (true) {
        ZeroMemory(buffer, sizeof(buffer));
        int bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            break;  // Client disconnected
        }

        std::string message(buffer, 0, bytesReceived);
        std::string fullMessage = "[" + clientName + "] " + message;

        // Handle channel switching and broadcasting
        if (message.substr(0, 6) == "/join ") {
            std::string newChannelName = message.substr(6);
            newChannelName.erase(remove_if(newChannelName.begin(), newChannelName.end(), isspace), newChannelName.end());  // Trim whitespace

            bool found = false;
            for (auto& channel : serverConfig.channels) {
                if (channel.name == newChannelName) {
                    // Remove client from the old channel
                    if (currentChannel) {
                        currentChannel->clients.erase(
                            std::remove(currentChannel->clients.begin(), currentChannel->clients.end(), sock),
                            currentChannel->clients.end()
                        );
                    }

                    // Add client to the new channel
                    currentChannel = &channel;
                    currentChannel->clients.push_back(sock);
                    std::string switchMsg = "Switched to channel: " + newChannelName + "\n";
                    send(sock, switchMsg.c_str(), switchMsg.size(), 0);
                    found = true;
                    break;
                }
            }

            if (!found) {
                std::string errorMsg = "Channel not found: " + newChannelName + "\n";
                send(sock, errorMsg.c_str(), errorMsg.size(), 0);
            }
        }

        else {
            for (SOCKET clientSock : currentChannel->clients) {
                if (clientSock != sock) {  // Do not echo the message back to the sender
                    send(clientSock, fullMessage.c_str(), fullMessage.length(), 0);
                }
            }
        }
    }

    // Client disconnects
    currentChannel->clients.erase(
        std::remove(currentChannel->clients.begin(), currentChannel->clients.end(), sock),
        currentChannel->clients.end()
    );
    closesocket(sock);
}



void start_server(ServerConfig serverConfig) {
    WSADATA wsData;
    WSAStartup(MAKEWORD(2, 2), &wsData);

    std::string serverIP = getLocalIPAddress();
    std::cout << "Starting server on " << serverIP << ":" << serverConfig.port << std::endl;

    SOCKET listening = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in hint;
    hint.sin_family = AF_INET;
    hint.sin_port = htons(serverConfig.port);
    hint.sin_addr.S_un.S_addr = INADDR_ANY;

    bind(listening, (sockaddr*)&hint, sizeof(hint));
    listen(listening, SOMAXCONN);

    while (true) {
        sockaddr_in client;
        int clientSize = sizeof(client);

        SOCKET clientSocket = accept(listening, (sockaddr*)&client, &clientSize);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Error accepting connection" << std::endl;
            continue;
        }

        // Pass both the socket and server configuration to the handling thread
        std::thread t(handle_connection, clientSocket, std::ref(serverConfig));
        t.detach();
    }

    closesocket(listening);
    WSACleanup();
}

void start_client(const std::string& ipAddress, int port, const std::string& username) {
    WSADATA wsData;
    WSAStartup(MAKEWORD(2, 2), &wsData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in hint;
    hint.sin_family = AF_INET;
    hint.sin_port = htons(port);
    inet_pton(AF_INET, ipAddress.c_str(), &hint.sin_addr);

    if (connect(sock, (sockaddr*)&hint, sizeof(hint)) == SOCKET_ERROR) {
        std::cerr << "Can't connect to server, Err #" << WSAGetLastError() << std::endl;
        closesocket(sock);
        WSACleanup();
        return;
    }

    // Send the username right after connecting
    send(sock, username.c_str(), username.size() + 1, 0);

    std::thread t([sock]() {
        char buffer[1024];
        while (true) {
            ZeroMemory(buffer, sizeof(buffer));
            int bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
            if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
                std::cout << "Disconnected from server." << std::endl;
                break;
            }
            std::cout << std::string(buffer, 0, bytesReceived) << std::endl << "> ";
        }
        });
    t.detach();

    std::string input;
    while (true) {
        std::cout << "> ";
        std::getline(std::cin, input);
        if (!input.empty()) {
            send(sock, input.c_str(), input.size() + 1, 0);
        }
    }

    closesocket(sock);
    WSACleanup();
}

int main() {
    WSADATA wsData;
    WSAStartup(MAKEWORD(2, 2), &wsData);

    while (true) {
        std::string mode;
        std::cout << "Enter mode (server, client, save, list, run, exit): ";
        std::cin >> mode;
        std::cin.ignore();

        if (mode == "server") {
            ServerConfig serverConfig;
            std::cout << "Enter port number for the server: ";
            std::cin >> serverConfig.port;
            std::cin.ignore();

            start_server(serverConfig);
        }
        else if (mode == "client") {
            std::cout << "1. Browse servers (not implemented)\n2. Enter server IP\nSelect an option: ";
            int choice;
            std::cin >> choice;
            std::cin.ignore();

            if (choice == 1) {
                std::cout << "Server browsing functionality is not implemented yet." << std::endl;
            }
            else if (choice == 2) {
                std::string ipAddress;
                std::cout << "Enter server IP: ";
                std::getline(std::cin, ipAddress);

                int port;
                std::cout << "Enter port number: ";
                std::cin >> port;
                std::cin.ignore();

                std::string username;
                std::cout << "Enter your username: ";
                std::getline(std::cin, username);

                start_client(ipAddress, port, username);
            }
        }
        else if (mode == "save") {
            ServerConfig newConfig;
            std::cout << "Enter name for the server config: ";
            std::getline(std::cin, newConfig.name);
            newConfig.ipAddress = getLocalIPAddress();
            std::cout << "IP address: " << newConfig.ipAddress << std::endl;
            std::cout << "Enter port: ";
            std::cin >> newConfig.port;
            std::cin.ignore();

            std::cout << "Enter channels (comma-separated, no spaces, e.g., general,tech,support): ";
            std::string channelsInput;
            std::getline(std::cin, channelsInput);
            std::istringstream iss(channelsInput);
            std::string channelName;
            while (getline(iss, channelName, ',')) {
                Channel channel;
                channel.name = channelName;
                // Assume that other properties of Channel can be set here as needed
                newConfig.channels.push_back(channel);
            }

            saveServerConfig(newConfig);
        }


        else if (mode == "list") {
            listServerConfigs(true);
        }
        else if (mode == "run") {
            auto configs = listServerConfigs(false);
            std::cout << "Enter the number of the server config to run: ";
            int choice;
            std::cin >> choice;
            std::cin.ignore();

            if (choice > 0 && choice <= configs.size()) {
                const auto& config = configs[choice - 1];
                std::cout << "Running " << config.name << " on " << config.ipAddress << ":" << config.port << std::endl;
                start_server(config);  // Pass the entire config object
            }
            else {
                std::cerr << "Invalid selection." << std::endl;
            }
        }

        else if (mode == "exit") {
            break;
        }
        else {
            std::cerr << "Invalid mode! Use 'server', 'client', 'save', 'list', 'run' or 'exit'." << std::endl;
        }
    }


    WSACleanup();
    return 0;
}
