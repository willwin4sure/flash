# Flash

C++ framework built for message passing in a
client-server model, abstracting away complicated
ASIO networking and multithreading.

Flash is based on a
[video series by javidx9](https://www.youtube.com/watch?v=2hNdkYInj4g),
with various modifications.

## Overview

Flash is designed so that all calling code can be
single-threaded. Under the hood, incoming and outgoing
messages are stored in a thread-safe queue and processed
asynchronously by a separate thread.

The client interface is rather simple, and can be found
in `flash/iclient.hpp`. The server interface can be found
in `flash/iserver.hpp`. Every class you see is templated
on a parameter `T`, which should be an enum class containing
all supported message types, up to the discretion of the user.

The server assigns a unique `UserId` to each connected
client, which can be used to target the sending of messages.
The server can also broadcast messages to all connected clients.
The server serializes all incoming client messages in
a single thread-safe queue.

The user may implement custom functionality for the server
by overriding the virtual functions in the `flash/iserverext.hpp`
interface. These allow you to react to certain events such as
client connections and disconnections, as well as incoming
messages.

## Features

Flash has a TCP implementation based on `javidx9`'s code,
with various modificaitons. This can be used when reliability
of messages is vital or when real-time performance is not
a priority. This code is found under `flash/tcp`,
and all relevant classes are in the `flash::tcp` namespace.

However, because Flash was primarily built for real-time
action multiplayer games, it also has a UDP implementation
that shares the same interface. This code is found under
`flash/udp`, and all relevant classes are in the `flash::udp`
namespace.

We follow the end-to-end principle and delegate concerns
of serialization or reliability to the application-level user,
allowing greater flexibility when using UDP.
For example, the user may want to implement
sequence numbers or message acknowledgement/retransmission
for important messages. Even the initial connection is
not guaranteed, and may need to be re-tried if it doesn't
succeed.

Since the connections are assumed to be disconnected
after a few seconds of no received messages,
you may also want to implement some sort of heartbeat
protocol if there aren't constant transmissions
in normal use.

## How to Develop

You need C++, CMake, and [Boost](https://www.boost.org/).

If you are on a VSCode environment with the CMake extension,
I would recommend adding the path to your Boost library
to the CMake configuration settings. You can find this by
clicking the cog wheel icon in the CMake extension side bar.

## How to Build

One option is to run the following sequence of commands:

```bash
mkdir build
cd build
cmake -DBOOST_ROOT=/path/to/boost ..
make
ctest
```

Alternatively, you can just click the Build button with
the CMake extension in VSCode.

## Helpful links

* https://stackoverflow.com/questions/69457434/c-udp-server-io-context-running-in-thread-exits-before-work-can-start
* https://stackoverflow.com/questions/5344809/boost-asio-async-send-question
