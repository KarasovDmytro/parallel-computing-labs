#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

const int PORT = 7777;

std::string readFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void handleClient(SOCKET clientSocket) {
    char buffer[4096] = { 0 };

    int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (bytesRead > 0) {
        std::istringstream iss(buffer);
        std::string method, path, version;

        iss >> method >> path >> version;

        if (method == "GET") {
            if (path == "/") {
                path = "/index.html";
            }

            std::string localPath = path.substr(1);

            std::string content = readFile(localPath);
            std::string response;

            if (!content.empty()) {
                response = "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=UTF-8\r\n"
                    "Content-Length: " + std::to_string(content.size()) + "\r\n"
                    "Connection: close\r\n\r\n" +
                    content;
                std::cout << "[200 OK] " << path << std::endl;
            }
            else {
                std::string notFoundHtml = "<html><body><h1>404 Not Found</h1><p>The requested page was not found.</p></body></html>";
                response = "HTTP/1.1 404 Not Found\r\n"
                    "Content-Type: text/html; charset=UTF-8\r\n"
                    "Content-Length: " + std::to_string(notFoundHtml.size()) + "\r\n"
                    "Connection: close\r\n\r\n" +
                    notFoundHtml;
                std::cout << "[404 Not Found] " << path << std::endl;
            }
            send(clientSocket, response.c_str(), response.size(), 0);
        }
    }
    closesocket(clientSocket);
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, SOMAXCONN);

    std::cout << "HTTP Server is running on http://localhost:" << PORT << "\n";
    std::cout << "Waiting for connections...\n";

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket != INVALID_SOCKET) {
            std::thread(handleClient, clientSocket).detach();
        }
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}