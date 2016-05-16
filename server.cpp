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

int main(int argc, char** argv) {
  if (argc != 2) {
    std::printf("Usage: %s port_to_bind\n", argv[0]);
    return 1;
  }
  int port = std::atoi(argv[1]);
  LaunchChat(port);
  return 0;
}

