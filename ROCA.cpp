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

#pragma comment(lib, "ws2_32.lib")

std::mutex clientMutex;
std::map<SOCKET, std::string> clientNames; // Correct declaration of std::map
std::vector<SOCKET> clientSockets;

const std::string configFileName = "server_config.txt";
bool isAltEPressed() {
    return (GetAsyncKeyState(VK_MENU) & 0x8000) && (GetAsyncKeyState(0x45) & 0x8000);
}

struct ServerConfig {
    std::string name;
    std::string ipAddress;
    int port;
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

        configs[index] = { name, ip, port };
        std::cout << "Configuration updated." << std::endl;
    }
    else {
        std::cerr << "Invalid configuration index." << std::endl;
    }
}

void saveServerConfig(const ServerConfig& config) {
    std::ofstream outFile("server_config.txt", std::ios::app);  // Open in append mode
    if (outFile) {
        outFile << config.name << "," << config.ipAddress << "," << config.port << "\n";
    } else {
        std::cerr << "Could not open the config file for writing." << std::endl;
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
    std::ifstream inFile("server_config.txt");
    std::string line;

    if (inFile.is_open()) {
        while (getline(inFile, line)) {
            std::istringstream iss(line);
            std::string name, ip;
            int port;
            if (getline(iss, name, ',') && getline(iss, ip, ',') && iss >> port) {
                configs.push_back({ name, ip, port });
            }
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

void handle_connection(SOCKET sock) {
    clientMutex.lock();
    clientSockets.push_back(sock);  // Add new connection to the client list
    clientMutex.unlock();

    char buffer[1024];
    std::string clientName;

    // First message received should be the username
    ZeroMemory(buffer, sizeof(buffer));
    int bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
    if (bytesReceived > 0) {
        clientName = std::string(buffer, 0, bytesReceived);
        clientMutex.lock();
        clientNames[sock] = clientName;
        clientMutex.unlock();

        std::string joinMsg = clientName + " has joined the chat.\n";
        std::cout << joinMsg;
        broadcastMessage(joinMsg, sock);
    }
    else {
        std::cout << "Failed to receive username from client." << std::endl;
        closesocket(sock);
        return;
    }

    while (true) {
        ZeroMemory(buffer, sizeof(buffer));
        bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            clientMutex.lock();
            std::cout << clientName << " disconnected." << std::endl;
            clientNames.erase(sock);
            clientMutex.unlock();
            break;
        }

        std::string msg = "[" + clientName + "] " + std::string(buffer, 0, bytesReceived);
        std::cout << msg << std::endl;
        broadcastMessage(msg, sock);
    }

    clientMutex.lock();
    clientSockets.erase(std::remove(clientSockets.begin(), clientSockets.end(), sock), clientSockets.end());
    std::string leaveMsg = clientName + " left the chat.\n";
    std::cout << leaveMsg;
    broadcastMessage(leaveMsg);
    clientMutex.unlock();

    closesocket(sock);
}



void start_server(int port) {
    WSADATA wsData;
    WSAStartup(MAKEWORD(2, 2), &wsData);

    std::string serverIP = getLocalIPAddress();
    std::cout << "Starting server on " << serverIP << ":" << port << std::endl;

    SOCKET listening = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in hint;
    hint.sin_family = AF_INET;
    hint.sin_port = htons(port);
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

        std::thread t(handle_connection, clientSocket);
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
            int port;
            std::cout << "Enter port number for the server: ";
            std::cin >> port;
            std::cin.ignore();

            start_server(port);
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
            std::string name, ip;
            int port;
            std::cout << "Enter name for the server config: ";
            std::getline(std::cin, name);
            std::cout << "IP address: " << getLocalIPAddress() << std::endl;
            ip = getLocalIPAddress();
            std::cout << "Enter port: ";
            std::cin >> port;
            std::cin.ignore();

            ServerConfig newConfig{ name, ip, port };
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
            if (choice > 0 && choice <= configs.size()) {
                const auto& config = configs[choice - 1];
                std::cout << "Running " << config.name << " on " << config.ipAddress << ":" << config.port << std::endl;
                start_server(config.port);
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
