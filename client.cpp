#include <iostream>
#include <thread>
#include "socket.h"
#include <cstdio>
#include <sstream>

void SendData(const int port) {
  TSocket s;
  s.Connect("127.0.0.1", port);
  // I don't want server-socket to block during recv
  fcntl(s.GetSocket(), F_SETFL, O_NONBLOCK);
  // I also don't want to block in line "std::cin >> word;"
  // That's why let's use 'select' function. We sleep on it
  // until events in sockets or streams. Timeout is to prevent
  // infinite sleeping.

  for (;;) {
    // Preparing a set of sockets to be checked for events
    const int max_socket_to_check = STDIN_FILENO;
    timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(STDIN_FILENO, &readset);
    if (select(max_socket_to_check + 1, &readset, NULL, NULL, &timeout) <= 0) {
      // Timeout! We have waited for event in std::cin too long. Let's check
      // if server sent us message about end of chat.
      char buf[1024];
      int res = recv(s.GetSocket(), buf, sizeof(buf), 0);
      if (res == 3 && std::string(buf) == "BYE") {
        std::cout << "End of chat!" << std::endl;
        return;
      }
    } else {
      // Event happened in std::cin. We react, i. e. we read word and send it to server.
      std::string word;
      std::cin >> word;
      word += " ";
      s.Send(word.c_str(), word.size());
    }
  }
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::printf("Usage: %s port_to_connect\n", argv[0]);
    return 1;
  }

  int port = std::atoi(argv[1]);
  SendData(port);
  return 0;
}

