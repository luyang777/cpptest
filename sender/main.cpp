#include <iostream>
#include <string>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

void sendMessage(SOCKET sock, sockaddr_in& serverAddress, const std::string& message)
{
    sendto(
        sock,
        message.c_str(),
        static_cast<int>(message.size()),
        0,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress)
    );

    std::cout << "Sent: " << message << '\n';
}

int main()
{
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (sock == INVALID_SOCKET)
    {
        std::cerr << "Failed to create socket\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(5000);

    inet_pton(
        AF_INET,
        "127.0.0.1",
        &serverAddress.sin_addr
    );

    // Valid messages from different applications
    std::vector<std::string> messages =
    {
        R"({"timestamp":1755544291,"app":"APP_A","message":"Application started"})",

        R"({"timestamp":1755544292,"app":"APP_B","message":"Database connection established"})",

        R"({"timestamp":1755544293,"app":"APP_C","message":"Processing request"})",

        R"({"timestamp":1755544294,"app":"APP_A","message":"User logged in"})",

        R"({"timestamp":1755544295,"app":"APP_B","message":"Request completed successfully"})",

        // Out-of-order timestamp
        R"({"timestamp":1755544280,"app":"APP_C","message":"Older message received later"})",

        // Malformed JSON
        R"({"timestamp":1755544296,"app":"APP_A","message":"Missing closing brace")",

        // Missing message field
        R"({"timestamp":1755544297,"app":"APP_B"})",

        // Missing app field
        R"({"timestamp":1755544298,"message":"Application error"})",

        // Missing timestamp field
        R"({"app":"APP_C","message":"Missing timestamp"})",

        // Completely invalid data
        "This is not JSON"
    };

    for (const auto& message : messages)
    {
        sendMessage(sock, serverAddress, message);
    }

    std::cout << "\nAll test messages sent.\n";

    closesocket(sock);
    WSACleanup();

    return 0;
}