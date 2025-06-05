#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <thread>
#include <atomic>
#include <string>
#include <vector>
using namespace std;

int cleanupAndExit(string errorMessage, ADDRINFO* addr = NULL, SOCKET sock = INVALID_SOCKET) {
    cerr << errorMessage << endl;

    if (sock != INVALID_SOCKET)
        closesocket(sock);
    if (addr != NULL)
        freeaddrinfo(addr);
    WSACleanup();
    return 1;
}

ADDRINFO prepareHints() {
    ADDRINFO hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;          // IPv4
    hints.ai_socktype = SOCK_STREAM;    // TCP
    hints.ai_protocol = IPPROTO_TCP;
    return hints;
}

ADDRINFO* resolveAddress(ADDRINFO hints, string port) {
    ADDRINFO* result = NULL;
    if (getaddrinfo("localhost", port.c_str(), &hints, &result) != 0)
        return nullptr;
    return result;
}

void sendMessage(SOCKET sock, const string& message) {
    uint32_t msgLength = htonl(message.size());
    send(sock, reinterpret_cast<const char*>(&msgLength), sizeof(msgLength), 0);
    send(sock, message.c_str(), message.size(), 0);
}

string receiveMessage(SOCKET sock) {
    uint32_t msgLength;
    if (recv(sock, reinterpret_cast<char*>(&msgLength), sizeof(msgLength), 0) <= 0) {
        return "";
    }
    msgLength = ntohl(msgLength);

    vector<char> buffer(msgLength);
    int bytesReceived = recv(sock, buffer.data(), msgLength, 0);
    if (bytesReceived <= 0) {
        return "";
    }

    return string(buffer.data(), bytesReceived);
}

enum class ClientState {
    InMenu,
    InPublicChat,
    InPrivateChat,
    WaitingPrivateResponse
};

atomic<ClientState> currentState = ClientState::InMenu;
string currentChatPartner;

void receiveMessages(SOCKET sock, atomic<bool>& running) {
    while (running) {
        string message = receiveMessage(sock);
        if (message.empty()) {
            cerr << "Disconnected from server.\n";
            running = false;
            break;
        }

        if (message == "CHAT_MODE_ACTIVE") {
            currentState = ClientState::InPublicChat;
            cout << "=== Entered public chat ===" << endl;
            cout << "Type 'exit' to leave chat" << endl;
            cout << "> ";
        }
        else if (message == "CHAT_MODE_EXIT") {
            currentState = ClientState::InMenu;
            cout << "=== Left public chat ===" << endl;
            cout << "> ";
        }
        else if (message.find("PRIVATE_REQUEST:") == 0) {
            currentState = ClientState::WaitingPrivateResponse;
            currentChatPartner = message.substr(16);
            cout << currentChatPartner << " wants to start a private chat. Accept? (yes/no): ";
        }
        else if (message.find("PRIVATE_CHAT_STARTED:") == 0) {
            currentState = ClientState::InPrivateChat;
            cout << "=== Private chat with " << message.substr(20) << " ===" << endl;
            cout << "Type 'exit' to leave chat" << endl;
            cout << "> ";
        }
        else if (message.find("PRIVATE_MSG:") == 0) {
            cout << "[Private] " << message.substr(12) << endl;
            cout << "> ";
        }
        else if (message.find("PRIVATE_CHAT_ENDED") == 0) {
            currentState = ClientState::InMenu;
            cout << "=== Private chat ended ===" << endl;
            cout << "> ";
        }
        else if (message.find("CHAT_MSG:") == 0) {
            cout << "[Public] " << message.substr(9) << endl;
            cout << "> ";
        }
        else {
            cout << message << endl;
            cout << "> ";
        }
    }
}

void handleUserInput(SOCKET connectSocket, atomic<bool>& running) {
    string input;
    while (running) {
        cout << "> ";
        getline(cin, input);

        if (input.empty()) continue;

        if (input == "/exit") {
            running = false;
            break;
        }

        if (currentState == ClientState::WaitingPrivateResponse) {
            if (input == "yes" || input == "no") {
                sendMessage(connectSocket, input);
                currentState = ClientState::InMenu;
                continue;
            }
            else {
                cout << "Please answer 'yes' or 'no'" << endl;
                continue;
            }
        }

        sendMessage(connectSocket, input);
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed\n";
        return 1;
    }

    ADDRINFO hints = prepareHints();
    string port = "1111";
    ADDRINFO* addrResult = resolveAddress(hints, port);
    if (!addrResult)
        return cleanupAndExit("Failed to resolve address", addrResult);

    SOCKET connectSocket = socket(addrResult->ai_family, addrResult->ai_socktype, addrResult->ai_protocol);
    if (connectSocket == INVALID_SOCKET)
        return cleanupAndExit("Failed to create socket", addrResult);

    if (connect(connectSocket, addrResult->ai_addr, (int)addrResult->ai_addrlen) == SOCKET_ERROR)
        return cleanupAndExit("Connection failed", addrResult, connectSocket);

    freeaddrinfo(addrResult);

    string welcomeMsg = receiveMessage(connectSocket);
    cout << welcomeMsg << endl;

    atomic<bool> running(true);
    thread receiver(receiveMessages, connectSocket, ref(running));
    thread inputHandler(handleUserInput, connectSocket, ref(running));

    if (receiver.joinable()) receiver.join();
    if (inputHandler.joinable()) inputHandler.join();

    closesocket(connectSocket);
    WSACleanup();
    return 0;
}