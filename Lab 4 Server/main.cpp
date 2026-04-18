#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include "Packet.h"

#define MAX_VAL 1000

#pragma comment(lib, "Ws2_32.lib")

struct ClientState {
    int nThreads = 1;
    std::vector<int> data;
    std::atomic<int> processingStatus{ 0 }; // 0 - очікує, 1 - рахує, 2 - готово
    int resultMode = 0;
    double resultMedian = 0.0;
    uint32_t executionTimeMs = 0;
};

void freqCountThread(const std::vector<int>& data, size_t start, size_t end, std::vector<int>& localFreq) {
    for (size_t i = start; i < end; i++) {
        localFreq[data[i]]++;
    }
}

void solveParallel(const std::vector<int>& data, int nThreads, double& medianOut, int& modeOut) {

    std::vector<std::thread> threads;
    std::vector<std::vector<int>> localFreqs(nThreads, std::vector<int>(MAX_VAL + 1, 0));

    size_t chainSize = data.size() / nThreads;

    for (int i = 0; i < nThreads; i++) {
        size_t start = i * chainSize;
        size_t end = (i == nThreads - 1) ? data.size() : start + chainSize;
        threads.emplace_back(freqCountThread, std::cref(data), start, end, std::ref(localFreqs[i]));
    }

    for (auto& t : threads) {
        t.join();
    }

    std::vector<int> globalFreq(MAX_VAL + 1, 0);
    for (const auto& freq : localFreqs) {
        for (int i = 0; i <= MAX_VAL; i++) {
            globalFreq[i] += freq[i];
        }
    }

    int mode = 0;
    int maxCount = -1;

    for (int i = 0; i <= MAX_VAL; i++) {
        if (globalFreq[i] > maxCount) {
            maxCount = globalFreq[i];
            mode = i;
        }
    }
    modeOut = mode;

    long long count = 0;
    size_t n = data.size();

    int m1 = -1;
    int m2 = -1;

    size_t target1 = (n % 2 == 0) ? (n / 2) : (n / 2 + 1);
    size_t target2 = n / 2 + 1;

    for (int i = 0; i <= MAX_VAL; i++) {
        count += globalFreq[i];
        if (m1 == -1 && count >= target1) m1 = i;
        if (count >= target2) {
            m2 = i;
            break;
        }
    }

    medianOut = (m1 + m2) / 2.0;
}


void WorkerThread(std::shared_ptr<ClientState> state) {
    std::cout << "[Worker] Computing started...\n";

    auto startTime = std::chrono::steady_clock::now();
    solveParallel(state->data, state->nThreads, state->resultMedian, state->resultMode);
    auto endTime = std::chrono::steady_clock::now();

    state->executionTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    state->processingStatus = 2;
    std::cout << "[Worker] Computing ended!\n";
}

void ClientThread(SOCKET clientSocket) {
    std::cout << "[ClientThread] New client connected!\n";

    auto state = std::make_shared<ClientState>();

    while (true) {
        MessageHeader header;
        int bytesRead = recv(clientSocket, (char*)&header, sizeof(header), 0);

        if (bytesRead <= 0) {
            std::cout << "[ClientThread] Client disconnected." << std::endl;
            break;
        }

        uint32_t payloadSize = ntohl(header.payloadSize);

        switch (header.command) {
        case SEND_CONFIG: {
            std::cout << "[ClientThread] Got config: " << payloadSize << " bytes\n";

            std::vector<char> buffer(payloadSize);
            uint32_t totalReceived = 0;

            while (totalReceived < payloadSize) {
                int bytes = recv(clientSocket, buffer.data() + totalReceived, payloadSize - totalReceived, 0);
                if (bytes <= 0) {
                    std::cout << "Error when getting config.\n";
                    return;
                }
                totalReceived += bytes;
            }

            uint32_t rawThreads;
            memcpy(&rawThreads, buffer.data(), sizeof(uint32_t));
            state->nThreads = ntohl(rawThreads);

            uint32_t dataBytes = payloadSize - sizeof(uint32_t);
            uint32_t elementsCount = dataBytes / sizeof(uint32_t);
            state->data.resize(elementsCount);

            uint32_t* rawArray = reinterpret_cast<uint32_t*>(buffer.data() + sizeof(uint32_t));

            for (uint32_t i = 0; i < elementsCount; i++) {
                state->data[i] = ntohl(rawArray[i]);
            }

            std::cout << "[ClientThread] Array of " << elementsCount
                << " elements was accepted. Threads amount: " << state->nThreads << "\n";

            MessageHeader ack{ ACK_CONFIG, htonl(0) };
            send(clientSocket, (char*)&ack, sizeof(ack), 0);
            break;
        }
        case START: {
            std::cout << "[ClientThread] Starting work thread...\n";
            state->processingStatus = 1;

            std::thread(WorkerThread, state).detach();

            MessageHeader ack{ ACK_START, htonl(0) };
            send(clientSocket, (char*)&ack, sizeof(ack), 0);
            break;
        }
        case GET_STATUS: {
            if (state->processingStatus == 1) {
                MessageHeader res{ STATUS, htonl(0) };
                send(clientSocket, (char*)&res, sizeof(res), 0);
            }
            else if (state->processingStatus == 2) {
                uint32_t payloadSize = sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint32_t);
                MessageHeader res{ RESULT, htonl(payloadSize) };
                send(clientSocket, (char*)&res, sizeof(res), 0);

                uint32_t netMode = htonl(state->resultMode);
                send(clientSocket, (char*)&netMode, sizeof(netMode), 0);

                uint64_t netMedian;
                uint64_t hostMedian;
                memcpy(&hostMedian, &state->resultMedian, sizeof(double));
                netMedian = htonll(hostMedian);

                send(clientSocket, (char*)&netMedian, sizeof(netMedian), 0);

                uint32_t netTime = htonl(state->executionTimeMs);
                send(clientSocket, (char*)&netTime, sizeof(netTime), 0);

                state->processingStatus = 0;
            }
            break;
        }
        }
    }
    closesocket(clientSocket);
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WinSock Init Error!\n";
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));

    listen(serverSocket, SOMAXCONN);
    std::cout << "[Main] Server started on port 7777. Waiting for clients...\n";

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket != INVALID_SOCKET) {
            std::thread(ClientThread, clientSocket).detach();
        }
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}