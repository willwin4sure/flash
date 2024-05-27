#include <flash/message.hpp>
#include <flash/ts_deque.hpp>

#include <flash/udp/client.hpp>
#include <flash/udp/server.hpp>

#include "CustomMsgTypes.hpp"


class CustomClient : public flash::udp::client<CustomMsgTypes> {
public:
    void PingServer() {
        flash::message<CustomMsgTypes> msg { CustomMsgTypes::ServerPing };

        // Let's try to figure out the round-time trip time, will work if running on same clock.
        std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();
        
        msg << timeNow;
        Send(std::move(msg));
    }

    void SendToAll() {
        flash::message<CustomMsgTypes> msg { CustomMsgTypes::MessageAll };
        Send(std::move(msg));
    }
};

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <client_num> <server_ip_address>\n";
        return 1;
    }

    int clientNum = std::stoi(argv[1]);
    std::string serverIpAddress = argv[2];

    CustomClient c;
    c.Connect(serverIpAddress, 38766);

    bool quit = false;
    while (!quit) {

        if (GetKeyState('0' + clientNum) & 0x8000) {
            if (GetKeyState('P') & 0x8000) {
                Sleep(1000);
                std::cout << "Ping Server" << std::endl;
                c.PingServer();
            }

            if (GetKeyState('A') & 0x8000) {
                Sleep(1000);
                std::cout << "Send to all" << std::endl;
                c.SendToAll();
            }
        }

        if (c.IsConnected()) {
            if (!c.Incoming().empty()) {
                auto msg = c.Incoming().pop_front().m_msg;

                switch(msg.get_header().m_type) {
                case CustomMsgTypes::ServerPing: {
                    // Server responded to a Ping request.
                    std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();
                    std::chrono::system_clock::time_point timeThen;
                    msg >> timeThen;

                    std::cout << "Ping: " << std::chrono::duration<double>(timeNow - timeThen).count() << "s" << std::endl;
                }
                break;
                case CustomMsgTypes::MessageAll: {
                    std::cout << "Server: Message to all" << std::endl;
                }
                break;
                case CustomMsgTypes::ClientDisconnect: {
                    // Server has responded to a message.
                    uint32_t clientId;
                    msg >> clientId;

                    std::cout << "Client [" << clientId << "] Disconnected." << std::endl;
                }
                break;
                }
            }

        } else {
            std::cout << "Server Down" << std::endl;
            quit = true;
        }
    }

    c.Disconnect();

    return 0;
}