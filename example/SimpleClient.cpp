#include "flash/message.hpp"
#include "flash/ts_deque.hpp"

#include "flash/tcp/client.hpp"
#include "flash/tcp/connection.hpp"
#include "flash/tcp/server.hpp"

#include "CustomMsgTypes.hpp"

class CustomClient : public flash::tcp::client<CustomMsgTypes> {
public:
    void PingServer() {
        flash::message<CustomMsgTypes> msg { CustomMsgTypes::ServerPing };

        // Let's try to figure out the round-time trip time, will work if running on same clock.
        std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();
        
        msg << timeNow;
        Send(std::move(msg));
    }
};

int main() {
    CustomClient c;
    c.Connect("127.0.0.1", 60000);

    int ticks = 0;

    bool quit = false;
    while (!quit) {

        if (ticks == 100000) {   
            std::cout << "Pinging Server\n";
            c.PingServer();
        }

        if (c.IsConnected()) {
            if (!c.Incoming().empty()) {
                auto msg = c.Incoming().pop_front().m_msg;

                switch(msg.m_header.m_type) {
                case CustomMsgTypes::ServerPing: {
                    // Server responded to a Ping request.
                    std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();
                    std::chrono::system_clock::time_point timeThen;
                    msg >> timeThen;

                    std::cout << "Ping: " << std::chrono::duration<double>(timeNow - timeThen).count() << "s\n";
                }
                break;
                }
            }

        } else {
            std::cout << "Server Down\n";
            quit = true;
        }

        ++ticks;
    }

    return 0;
}