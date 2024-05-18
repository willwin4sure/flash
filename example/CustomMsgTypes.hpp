#ifndef CUSTOMMSGTYPES_HPP
#define CUSTOMMSGTYPES_HPP

enum class CustomMsgTypes : uint32_t {
    ServerAccept,
    ServerDeny,
    ServerPing,
    MessageAll,
    ServerMessage,
    ClientDisconnect
};

#endif