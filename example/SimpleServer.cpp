#include <flash/message.hpp>
#include <flash/ts_deque.hpp>

#include <flash/udp/client.hpp>
#include <flash/udp/server.hpp>

#include "CustomMsgTypes.hpp"


class CustomServer : public flash::udp::server<CustomMsgTypes> {
public:
    CustomServer(uint16_t port) : flash::udp::server<CustomMsgTypes>(port) {
        
    }

protected:
    bool OnClientConnect(const boost::asio::ip::address& address) override {
        // Allow everyone in.
        return true;
    }

    void OnClientValidate(flash::UserId clientId) override {
        
    }

    void OnClientDisconnect(flash::UserId clientId) override {
        std::cout << "[" << clientId << "] Disconnected.\n";
        flash::message<CustomMsgTypes> msg { CustomMsgTypes::ClientDisconnect };
        msg << clientId;
        MessageAllClients(std::move(msg));
    }

    void OnMessage(flash::UserId clientId, flash::message<CustomMsgTypes>&& msg) override {
        switch (msg.get_header().m_type) {
        case CustomMsgTypes::ServerPing: {
            // Simply bounce the message back to the client.
            MessageClient(clientId, std::move(msg));
        }
        break;
        case CustomMsgTypes::MessageAll: {
            // Simply bounce the message back to all clients, except sender.
            MessageAllClients(std::move(msg), clientId);
        }
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