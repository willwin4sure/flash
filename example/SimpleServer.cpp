#include "flash/message.hpp"
#include "flash/ts_deque.hpp"

#include "flash/tcp/client.hpp"
#include "flash/tcp/connection.hpp"
#include "flash/tcp/server.hpp"

#include "CustomMsgTypes.hpp"

class CustomServer : public flash::tcp::server<CustomMsgTypes> {
public:
    CustomServer(uint16_t port) : flash::tcp::server<CustomMsgTypes>(port) {
        
    }

protected:
    bool OnClientConnect(const boost::asio::ip::tcp::socket& socket) override {
        return true;
    }

    void OnClientDisconnect(flash::UserId clientId) override {

    }

    void OnMessage(flash::UserId clientId, flash::message<CustomMsgTypes>&& msg) override {
        switch (msg.m_header.m_type) {
        case CustomMsgTypes::ServerPing:
        {
            std::cout << "[" << clientId << "] Server Ping\n";

            // Simply bounce the message back to the client.
            MessageClient(clientId, std::move(msg));
        }
        break;
        }
    }
};

int main() {
    CustomServer server(60000);
    server.Start();

    while (true) {
        server.Update();
    }

    return 0;
}