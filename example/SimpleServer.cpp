#include "client.hpp"
#include "connection.hpp"
#include "message.hpp"
#include "server.hpp"
#include "ts_deque.hpp"

#include "CustomMsgTypes.hpp"

class CustomServer : public flash::server<CustomMsgTypes> {
public:
    CustomServer(uint16_t port) : flash::server<CustomMsgTypes>(port) {
        
    }

protected:
    bool OnClientConnect(std::shared_ptr<flash::connection<CustomMsgTypes>> client) override {
        // flash::message<CustomMsgTypes> msg { CustomMsgTypes::ServerAccept };
        // client->Send(std::move(msg));
        return true;
    }

    void OnClientDisconnect(std::shared_ptr<flash::connection<CustomMsgTypes>> client) override {

    }

    void OnMessage(std::shared_ptr<flash::connection<CustomMsgTypes>> client, flash::message<CustomMsgTypes>& msg) override {
        switch (msg.m_header.m_type) {
        case CustomMsgTypes::ServerPing:
        {
            std::cout << "[" << client->GetId() << "] Server Ping\n";

            client->Send(std::move(msg));
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