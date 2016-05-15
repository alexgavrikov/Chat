#include <iostream>
#include <thread>
#include "socket.h"


class TAcceptHandler {
    private:
        TSocket Socket;

    public:
        bool HandleAcceptedSocket(TSocket sock) {
            Socket = sock;
            return true;
        }
        const TSocket &GetSocket() const {
            return Socket;
        }
};


TSocket CreateConnection(int port) {
    TSocket s;
    s.Bind(port, "");
    TAcceptHandler handler;
    s.AcceptLoop(handler);
    return handler.GetSocket();
}

void LaunchChat(int port) {
    TSocket s;
    s.Bind(port, "");
    s.AcceptLoop();
}


int main() {
  LaunchChat(13232);
//    TSocket s = CreateConnection(13232);
//
//    TDataHandler handler;
//    std::thread t([&s, &handler] () {
//        s.RecvLoop(handler);
//    });
//    t.join();
    return 0;
}

