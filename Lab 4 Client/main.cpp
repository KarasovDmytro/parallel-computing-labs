#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include "Packet.h"

#define MAX_VAL 1000

#pragma comment(lib, "Ws2_32.lib")

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WinSock Init Error!\n";
        return 1;
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    std::cout << "Connecting to server...\n";
    if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to the server.\n";
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }
    std::cout << "Connected successfully!\n\n";

    int nThreads = 4;
    int dataSize = 100000000;

    std::vector<int> testData(dataSize);

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dist(0, MAX_VAL);

    std::cout << "Generating array " << dataSize << " elements\n";
    for (int i = 0; i < dataSize; i++) {
        testData[i] = dist(rng);
    }
    std::cout << "Array generated. Sending config...\n";

    uint32_t payloadSize = sizeof(uint32_t) + (testData.size() * sizeof(uint32_t));
    std::vector<char> buffer(payloadSize);

    uint32_t netThreads = htonl(nThreads);
    memcpy(buffer.data(), &netThreads, sizeof(uint32_t));

    uint32_t* rawArray = reinterpret_cast<uint32_t*>(buffer.data() + sizeof(uint32_t));
    for (size_t i = 0; i < testData.size(); i++) {
        rawArray[i] = htonl(testData[i]);
    }

    MessageHeader configHeader{ SEND_CONFIG, htonl(payloadSize) };
    send(clientSocket, (char*)&configHeader, sizeof(configHeader), 0);
    send(clientSocket, buffer.data(), payloadSize, 0);

    MessageHeader ackHeader;
    recv(clientSocket, (char*)&ackHeader, sizeof(ackHeader), 0);
    if (ackHeader.command == ACK_CONFIG) {
        std::cout << "[Server] Configuration accepted.\n";
    }

    MessageHeader startHeader{ START, htonl(0) };
    send(clientSocket, (char*)&startHeader, sizeof(startHeader), 0);

    recv(clientSocket, (char*)&ackHeader, sizeof(ackHeader), 0);
    if (ackHeader.command == ACK_START) {
        std::cout << "[Server] Computing started.\n\n";
    }

    bool isDone = false;
    while (!isDone) 
    {
        MessageHeader statusReq{ GET_STATUS, htonl(0) };
        send(clientSocket, (char*)&statusReq, sizeof(statusReq), 0);

        MessageHeader statusRes;
        int bytes = recv(clientSocket, (char*)&statusRes, sizeof(statusRes), 0);

        if (bytes <= 0) {
            std::cout << "Connection lost.\n";
            break;
        }

        if (statusRes.command == STATUS) {
            std::cout << "[Server] STATUS: working...\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        else if (statusRes.command == RESULT) {
            std::cout << "[Server] Status READY!\n\n";

            uint32_t resPayloadSize = ntohl(statusRes.payloadSize);
            std::vector<char> resBuffer(resPayloadSize);

            int totalRead = 0;
            while (totalRead < resPayloadSize) {
                totalRead += recv(clientSocket, resBuffer.data() + totalRead, resPayloadSize - totalRead, 0);
            }

            uint32_t rawMode;
            memcpy(&rawMode, resBuffer.data(), sizeof(uint32_t));
            int mode = ntohl(rawMode);

            uint64_t rawMedian;
            memcpy(&rawMedian, resBuffer.data() + sizeof(uint32_t), sizeof(uint64_t));
            uint64_t hostMedian = ntohll(rawMedian);

            double median;
            memcpy(&median, &hostMedian, sizeof(double));

            uint32_t rawTime;
            memcpy(&rawTime, resBuffer.data() + sizeof(uint32_t) + sizeof(uint64_t), sizeof(uint32_t));
            uint32_t execTimeMs = ntohl(rawTime);

            std::cout << "--- RESULTS --- \n";
            std::cout << "Mode: " << mode << "\n";
            std::cout << "Median: " << median << "\n";
            std::cout << "Execution time: " << execTimeMs << " ms (" << execTimeMs / 1000.0 << " sec)\n";

            isDone = true;
        }
    }

    shutdown(clientSocket, SD_BOTH);
    closesocket(clientSocket);
    WSACleanup();

    std::cout << "\nPress ENTER to exit...";
    std::cin.get();
    return 0;
}