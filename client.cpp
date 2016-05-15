#include <iostream>
#include <thread>
#include "socket.h"
#include <cstdio>
#include <sstream>

void FetchHTTPData() {
  TSocket s;
  s.Connect("acm.timus.ru", 80);
  TDataHandler handler;
  std::atomic<bool> stop(false);
  std::thread t([&s, &handler, &stop] () {
    s.RecvLoop(handler, stop);
  });
  std::string data = "GET / HTTP/1.1\r\nHost: acm.timus.ru\r\n\r\n";
  s.Send(data.c_str(), data.size() + 1);
  t.join();
}

void SendData() {
  TSocket s;
  s.Connect("127.0.0.1", 1234);
  // I don't want server-socket to block during recv
//  fcntl(s.GetSocket(), F_SETFL, O_NONBLOCK);
  // I also don't want to block in line "std::cin >> word;"
  // That's why let's use 'select' function. We sleep in it
  // until events in sockets or streams. Timeout is to prevent
  // infinite sleeping.
  timeval timeout;
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  int max_socket_to_check = STDIN_FILENO;

  for (;;) {
    // Preparing a set of sockets to be checked for events
    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(STDIN_FILENO, &readset);
    if (select(max_socket_to_check + 1, &readset, NULL, NULL, &timeout)
        <= 0) {
      // Timeout! We have waited for event in std::cin too long. Let's check
      // if server sent us message about end of chat.
//      char buf[1024];
//      int res = recv(s.GetSocket(), buf, sizeof(buf), 0);
//      if (res == 4 && std::string(buf) == "BYE\0") {
//        std::cout << "End of chat!" << std::endl;
//        return;
//      }
    } else {
      // Event happened in std::cin. We react, i. e. we read word and send it to server.
      std::string word;
      std::cin >> word;
//      word += " ";
      if (word == "GET") {
        std::cout << "gh" << std::endl;
        word = "GET / HTTP\r\n\r\n";
      } else {
        std::stringstream stream;
        if (word == "ships") {
          static std::string kShips =
              "100:shipping:1111000000000000000011100011100000000000101010000010101000010000000000000100010000000000000000010000";
//              "100:shipping:111100000000000000000011100011100000000000101010000010101000010000000000000100010000000000000000010000";
          word = kShips;
      }
      stream << "POST\r\nContent-Length: ";
      stream << word.size() << "\r\n\r\n" << word;
      word = stream.str();
      std::cout << word << std::endl;
    }

    s.Send(word.c_str(), word.size());
    char buf[1000000] = "";
    int res = recv(s.GetSocket(), buf, sizeof(buf), 0);
    std::string gg(buf);
    std::cout << (int)gg[gg.size()-3]<<" "<<(int)gg[gg.size()-2]<<" "<<(int)gg[gg.size()-1]<<std::endl;
    fcntl(s.GetSocket(), F_SETFL, O_NONBLOCK);
    while (res > 0) {
      std::cout << "e" << buf << "e" << std::endl;
      res = recv(s.GetSocket(), buf, sizeof(buf), 0);
    }
    fcntl(s.GetSocket(), F_SETFL, 0);
  }
}
}

int main() {
//  FetchHTTPData();
SendData();
return 0;
}

