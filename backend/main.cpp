#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include "json.hpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

using json = nlohmann::json;

struct LogRecord
{
    std::uint64_t id;
    std::int64_t timestamp;
    bool hasTimestamp;
    std::string app;
    std::string message;
    std::string status;
    std::string raw;
};
std::vector<LogRecord> records;
std::uint64_t nextRecordId = 1;
std::optional<std::int64_t> latestTimestamp;
std::mutex recordsMutex;
std::atomic<bool> running{true};
SOCKET udpSocket = INVALID_SOCKET;
std::vector<SOCKET> webSocketClients;
std::mutex webSocketMutex;

json toJson(const LogRecord& record) { return {{"timestamp", record.timestamp}, {"app", record.app}, {"message", record.message}}; }

json toDisplayJson(const LogRecord& record)
{
    return {{"id", record.id},
        {"timestamp", record.hasTimestamp ? json(record.timestamp) : json(nullptr)},
        {"app", record.app.empty() ? json(nullptr) : json(record.app)},
        {"message", record.message.empty() ? json(nullptr) : json(record.message)},
        {"status", record.status}};
}

void appendError(const std::string& reason, const std::string& payload)
{
    const auto now = std::chrono::system_clock::now();
    const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::ofstream errorFile("../logs/error.log", std::ios::app);
    if (errorFile)
    {
        errorFile << json{{"timestamp", timestamp}, {"reason", reason}, {"payload", payload}}.dump() << '\n';
    }
}

bool sendAll(SOCKET clientSocket, const char* data, int length)
{
    int sent = 0;
    while (sent < length)
    {
        int result = send(clientSocket, data + sent, length - sent, 0);
        if (result == SOCKET_ERROR) return false;
        sent += result;
    }
    return true;
}

std::string base64Encode(const unsigned char* data, std::size_t length)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (std::size_t index = 0; index < length; index += 3)
    {
        const unsigned int block = (data[index] << 16) |
            ((index + 1 < length ? data[index + 1] : 0) << 8) |
            (index + 2 < length ? data[index + 2] : 0);
        result += alphabet[(block >> 18) & 63];
        result += alphabet[(block >> 12) & 63];
        result += index + 1 < length ? alphabet[(block >> 6) & 63] : '=';
        result += index + 2 < length ? alphabet[block & 63] : '=';
    }
    return result;
}

std::string webSocketAcceptKey(const std::string& clientKey)
{
    const std::string input = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[20]{};
    BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
    BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    BCryptHashData(hash, reinterpret_cast<unsigned char*>(const_cast<char*>(input.data())),
        static_cast<ULONG>(input.size()), 0);
    BCryptFinishHash(hash, digest, sizeof(digest), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return base64Encode(digest, sizeof(digest));
}

bool sendWebSocketText(SOCKET clientSocket, const std::string& message)
{
    std::vector<unsigned char> frame;
    frame.push_back(0x81);
    if (message.size() < 126)
    {
        frame.push_back(static_cast<unsigned char>(message.size()));
    }
    else if (message.size() <= 65535)
    {
        frame.push_back(126);
        frame.push_back(static_cast<unsigned char>((message.size() >> 8) & 255));
        frame.push_back(static_cast<unsigned char>(message.size() & 255));
    }
    else return false;
    frame.insert(frame.end(), message.begin(), message.end());
    return sendAll(clientSocket, reinterpret_cast<const char*>(frame.data()), static_cast<int>(frame.size()));
}

void broadcast(const std::string& message)
{
    std::lock_guard lock(webSocketMutex);
    for (auto client = webSocketClients.begin(); client != webSocketClients.end();)
    {
        if (sendWebSocketText(*client, message)) ++client;
        else
        {
            closesocket(*client);
            client = webSocketClients.erase(client);
        }
    }
}

void appendRecord(const LogRecord& record, bool valid)
{
    json displayRecord;
    {
        std::lock_guard lock(recordsMutex);
        records.push_back(record);
        displayRecord = toDisplayJson(record);
    }

    if (valid)
    {
        std::ofstream logFile("../logs/network.log", std::ios::app);
        if (logFile) logFile << toJson(record).dump() << '\n';
    }
    broadcast(json{{"type", "log"}, {"record", displayRecord}}.dump());
}

void loadRecords()
{
    // Restore valid records after a backend restart; ignore damaged lines.
    std::ifstream logFile("../logs/network.log");
    std::string line;
    while (std::getline(logFile, line))
    {
        try
        {
            json data = json::parse(line);
            if (data.contains("timestamp") && data["timestamp"].is_number() &&
                data.contains("app") && data["app"].is_string() &&
                data.contains("message") && data["message"].is_string())
            {
                LogRecord record{};
                record.id = nextRecordId++;
                record.timestamp = data["timestamp"].get<std::int64_t>();
                record.hasTimestamp = true;
                record.app = data["app"].get<std::string>();
                record.message = data["message"].get<std::string>();
                record.status = "Valid";
                record.raw = line;
                records.push_back(record);
                latestTimestamp = std::max(latestTimestamp.value_or(data["timestamp"].get<std::int64_t>()),
                    data["timestamp"].get<std::int64_t>());
            }
        }
        catch (const json::exception&) { }
    }
}

void loadErrorRecords()
{
    std::ifstream errorFile("../logs/error.log");
    std::string line;
    while (std::getline(errorFile, line))
    {
        try
        {
            json data = json::parse(line);
            if (data.contains("reason") && data["reason"].is_string() &&
                data.contains("payload") && data["payload"].is_string())
            {
                LogRecord record{};
                record.id = nextRecordId++;
                record.status = data["reason"].get<std::string>();
                record.raw = data["payload"].get<std::string>();
                records.push_back(record);
            }
        }
        catch (const json::exception&) { }
    }
}

void receiveLogs()
{
    // UDP preserves packet arrival order; the UI can sort by timestamp later.
    char buffer[8192];
    while (running)
    {
        sockaddr_in clientAddress{};
        int clientAddressSize = sizeof(clientAddress);
        int bytesReceived = recvfrom(udpSocket, buffer, sizeof(buffer) - 1, 0,
            reinterpret_cast<sockaddr*>(&clientAddress), &clientAddressSize);
        if (bytesReceived == SOCKET_ERROR)
        {
            if (running) std::cerr << "recvfrom failed\n";
            continue;
        }
        buffer[bytesReceived] = '\0';
        try
        {
            json data = json::parse(buffer);
            const bool hasTimestamp = data.contains("timestamp") && data["timestamp"].is_number_integer();
            const bool hasApp = data.contains("app") && data["app"].is_string();
            const bool hasMessage = data.contains("message") && data["message"].is_string();
            std::string reason;
            if (!hasTimestamp) reason += data.contains("timestamp") ? "Invalid timestamp; " : "Missing timestamp; ";
            if (!hasApp) reason += data.contains("app") ? "Invalid app; " : "Missing app; ";
            if (!hasMessage) reason += data.contains("message") ? "Invalid message; " : "Missing message; ";

            const std::int64_t timestamp = hasTimestamp ? data["timestamp"].get<std::int64_t>() : 0;
            const std::string app = hasApp ? data["app"].get<std::string>() : "";
            const std::string message = hasMessage ? data["message"].get<std::string>() : "";
            const bool valid = reason.empty();
            if (valid && latestTimestamp.has_value() && timestamp < latestTimestamp.value())
            {
                reason = "Out of order";
            }
            if (valid) latestTimestamp = std::max(latestTimestamp.value_or(timestamp), timestamp);

            LogRecord record{};
            record.id = nextRecordId++;
            record.timestamp = timestamp;
            record.hasTimestamp = hasTimestamp;
            record.app = app;
            record.message = message;
            record.status = reason.empty() ? "Valid" : reason;
            record.raw = buffer;
            if (!valid) appendError(reason, buffer);
            appendRecord(record, valid);
            std::cout << '[' << (app.empty() ? "unknown" : app) << "] " <<
                (message.empty() ? reason : message) << '\n';
        }
        catch (const json::exception& error)
        {
            appendError(error.what(), buffer);
            LogRecord record{};
            record.id = nextRecordId++;
            record.status = "Invalid JSON";
            record.raw = buffer;
            appendRecord(record, false);
            std::cerr << "Invalid JSON received: " << error.what() << '\n';
        }
    }
}

void sendResponse(SOCKET clientSocket, const std::string& body, const std::string& status = "200 OK")
{
    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\nContent-Type: application/json\r\n"
             << "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, DELETE, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\nContent-Length: " << body.size()
             << "\r\nConnection: close\r\n\r\n" << body;
    const std::string responseText = response.str();
    send(clientSocket, responseText.c_str(), static_cast<int>(responseText.size()), 0);
}

void handleWebSocket(SOCKET clientSocket, const std::string& request)
{
    const std::string headerName = "Sec-WebSocket-Key:";
    const std::size_t keyStart = request.find(headerName);
    if (keyStart == std::string::npos)
    {
        closesocket(clientSocket);
        return;
    }

    const std::size_t valueStart = request.find_first_not_of(" \t", keyStart + headerName.size());
    const std::size_t valueEnd = request.find("\r\n", valueStart);
    const std::string clientKey = request.substr(valueStart, valueEnd - valueStart);
    const std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
        webSocketAcceptKey(clientKey) + "\r\n\r\n";
    if (!sendAll(clientSocket, response.c_str(), static_cast<int>(response.size())))
    {
        closesocket(clientSocket);
        return;
    }

    {
        std::lock_guard lock(webSocketMutex);
        webSocketClients.push_back(clientSocket);
    }

    json snapshot = json::array();
    {
        std::lock_guard lock(recordsMutex);
        for (const LogRecord& record : records) snapshot.push_back(toDisplayJson(record));
    }
    if (!sendWebSocketText(clientSocket, json{{"type", "snapshot"}, {"records", snapshot}}.dump()))
    {
        shutdown(clientSocket, SD_BOTH);
        return;
    }

    char buffer[256];
    while (running && recv(clientSocket, buffer, sizeof(buffer), 0) > 0) { }
    shutdown(clientSocket, SD_BOTH);
    std::lock_guard lock(webSocketMutex);
    webSocketClients.erase(std::remove(webSocketClients.begin(), webSocketClients.end(), clientSocket), webSocketClients.end());
    closesocket(clientSocket);
}

void serveApi()
{
    // The React dashboard reads and clears records through this localhost API.
    SOCKET httpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (httpSocket == INVALID_SOCKET) return;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(5001);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(httpSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR || listen(httpSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "HTTP bind failed\n";
        closesocket(httpSocket);
        return;
    }
    while (running)
    {
        SOCKET clientSocket = accept(httpSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) continue;
        char buffer[4096]{};
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        std::string request = bytesReceived > 0 ? std::string(buffer, bytesReceived) : std::string{};
        if (request.find("Upgrade: websocket") != std::string::npos && request.compare(0, 7, "GET /ws") == 0)
        {
            std::thread(handleWebSocket, clientSocket, request).detach();
            continue;
        }
        if (request.compare(0, 7, "OPTIONS") == 0) sendResponse(clientSocket, "{}");
        else if (request.compare(0, 13, "GET /api/logs") == 0)
        {
            std::lock_guard lock(recordsMutex);
            json response = json::array();
            for (const LogRecord& record : records) response.push_back(toDisplayJson(record));
            sendResponse(clientSocket, response.dump());
        }
        else if (request.compare(0, 16, "DELETE /api/logs") == 0)
        {
            std::lock_guard lock(recordsMutex);
            records.clear();
            std::ofstream("../logs/network.log", std::ios::trunc);
            std::ofstream("../logs/error.log", std::ios::trunc);
            broadcast(R"({"type":"clear"})");
            sendResponse(clientSocket, "{}");
        }
        else sendResponse(clientSocket, "{\"error\":\"Not found\"}", "404 Not Found");
        closesocket(clientSocket);
    }
    closesocket(httpSocket);
}

int main()
{
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (udpSocket == INVALID_SOCKET)
    {
        std::cerr << "Failed to create socket\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(5000);
    inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr);

    if (bind(udpSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR)
    {
        std::cerr << "Bind failed\n";
        closesocket(udpSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Log listener started on 127.0.0.1:5000\n";
    std::cout << "Dashboard API and WebSocket available on 127.0.0.1:5001\n";
    loadRecords();
    loadErrorRecords();
    std::thread udpThread(receiveLogs);
    serveApi();
    running = false;
    shutdown(udpSocket, SD_BOTH);
    closesocket(udpSocket);
    udpThread.join();
    WSACleanup();
    return 0;
}