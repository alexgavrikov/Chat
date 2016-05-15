#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <memory>
#include <string>
#include <exception>
#include <list>
#include "thread_pool.h"
#include <atomic>
#include <mutex>
#include "fcntl.h"

class TDataHandler {
public:
  bool ProcessReceivedData(const char *data, size_t sz) {
    std::unique_lock<std::mutex> lock(mutex);
    for (; sz > 0; --sz, ++data)
      std::cout << *data;
    std::cout << std::flush;
    return false;
  }

private:
  std::mutex mutex;
};

class TSocket {
private:
  class TSocketHolder {
  private:
    int Socket;

    TSocketHolder(const TSocketHolder &) = delete;
    TSocketHolder(TSocketHolder &&) = delete;
    TSocketHolder &operator =(const TSocketHolder &) = delete;
    TSocketHolder &operator =(TSocketHolder &&) = delete;

  public:
    TSocketHolder()
        : Socket(socket(AF_INET, SOCK_STREAM, 0)) {
      if (Socket < 0)
        throw std::runtime_error("could not create socket");
    }
    TSocketHolder(int socket)
        : Socket(socket) {
    }
    ~TSocketHolder() {
      close(Socket);
    }
    int GetSocket() const {
      return Socket;
    }
  };
  using TSocketPtr = std::shared_ptr<TSocketHolder>;
  TSocketPtr listener_socket_holder_;

  bool ResolveHost(const std::string &host, int &addr) const {
    hostent *ent = gethostbyname(host.c_str());
    if (ent == nullptr)
      return false;
    for (size_t i = 0; ent->h_addr_list[i]; ++i) {
      addr = *reinterpret_cast<int**>(ent->h_addr_list)[i];
      return true;
    }
    return false;
  }

public:
  TSocket()
      : listener_socket_holder_(new TSocketHolder) {
  }
  TSocket(int sock)
      : listener_socket_holder_(new TSocketHolder(sock)) {
  }

  int GetSocket() const {
    return listener_socket_holder_->GetSocket();
  }

  void Connect(const std::string &host, int port) {
    int addr;
    if (!ResolveHost(host, addr))
      throw std::runtime_error("can't resolve host");
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = addr;
    if (connect(listener_socket_holder_->GetSocket(), reinterpret_cast<sockaddr*>(&address),
        sizeof(address)) < 0)
      throw std::runtime_error("can't connect");
  }
  void Bind(int port, const std::string &host) {
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (!host.empty()) {
      int addr;
      if (!ResolveHost(host, addr))
        throw std::runtime_error("can't resolve host");
      address.sin_addr.s_addr = addr;
    } else {
      address.sin_addr.s_addr = INADDR_ANY;
      std::cout << INADDR_ANY<<std::endl;
    }
    if (bind(listener_socket_holder_->GetSocket(), reinterpret_cast<sockaddr*>(&address),
        sizeof(address)) < 0)
      throw std::runtime_error("can't bind");
    if (listen(listener_socket_holder_->GetSocket(), 1) < 0)
      throw std::runtime_error("can't start listening");
  }

  // Old (original) AcceptLoop
  template<typename TAcceptHandler>
  void AcceptLoop(TAcceptHandler &handler) const {
    for (;;) {
      int sock = accept(listener_socket_holder_->GetSocket(), nullptr, nullptr);
      if (sock < 0)
        throw std::runtime_error("can't bind");
      TSocket res(sock);
      if (handler.HandleAcceptedSocket(res))
        return;
    }
  }

  // New AcceptLoop
  void AcceptLoop() const {
    static constexpr size_t kMinimalClientsCount = 2;
    static constexpr size_t kInitialThreadsCount = 2;
    static constexpr size_t kTimeOutInSeconds = 5;
    // I don't want listener-socket to block during accept
    fcntl(listener_socket_holder_->GetSocket(), F_SETFL, O_NONBLOCK);
    // Let's use 'select' function. We sleep in it
    // until events happen in sockets or streams.
    // Timeout is for preventing infinite sleeping.
    timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    int max_socket_to_check = listener_socket_holder_->GetSocket();

    ThreadPool threads_pool(kInitialThreadsCount);
    // One common datahandler for all threads. It contains mutex
    // to prevent mixing clients' messages.
    TDataHandler handler;
    // Main thread (which only accepts new clients and make new threads)
    // has to notify other threads about termination. This bool variable
    // is shared (available to all threads). And other threads regularly check
    // its value.
    std::atomic<bool> time_to_stop_all_threads(false);
    while (true) {
      // std::cout << "main: " << "start while-iteraton" << std::endl;
      // Preparing a set of sockets to be checked for events
      fd_set readset;
      FD_ZERO(&readset);
      FD_SET(listener_socket_holder_->GetSocket(), &readset);
      if (select(max_socket_to_check + 1, &readset, NULL, NULL, &timeout)
          <= 0) {
        // std::cout << "main: " << "time-out" << std::endl;
        if (threads_pool.UnfinishedTasksCount() < kMinimalClientsCount) {
          // Only one client in chat. Our chat doesn't make any sense.
          std::cout << "main: " << "Too few clients => break" << std::endl;
          break;
        }
      } else {
//        std::cout << "main: " << "else-section (accept found)"
//            << std::endl;
        int sock = accept(listener_socket_holder_->GetSocket(), nullptr, nullptr);
        if (sock < 0) {
          std::cout << "can't bind" << std::endl;
        } else {
          TSocket res(sock);
          auto future = threads_pool.enqueue(
              [&res, &handler, &time_to_stop_all_threads] () {
                res.RecvLoop(handler, time_to_stop_all_threads);
              });
        }
      }
    }

    // Thus we notify other threads that it is time to stop.
    time_to_stop_all_threads.store(true);
  }

  bool Send(const char *data, size_t sz) const {
    for (; sz > 0;) {
      int res = send(listener_socket_holder_->GetSocket(), data, sz, 0);
      if (res == 0)
        return false;
      if (res < 0)
        throw std::runtime_error("error in send");
      data += res;
      sz -= res;
    }
    return true;
  }

  // Helps for debugging.
  typedef std::chrono::microseconds Microseconds;
  typedef std::chrono::steady_clock Clock;
  typedef Clock::time_point Time;
  void sleep(unsigned milliseconds) const {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
  }

  // returns true if connection was closed by handler, false if connection was closed by peer
  template<typename TIOHandler>
  bool RecvLoop(TIOHandler &handler,
                const std::atomic<bool>& time_to_stop) const {
//    std::cout << "  non-main: " << "just started" << std::endl;
    // I don't want client-socket to block during recv
    fcntl(listener_socket_holder_->GetSocket(), F_SETFL, O_NONBLOCK);
    // Let's use 'select' function. We sleep in it
    // until events happen in sockets or streams.
    // Timeout is for preventing infinite sleeping.
    timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    int max_socket_to_check = listener_socket_holder_->GetSocket();

    while (!time_to_stop.load()) {
//      std::cout << "  non-main: " << "new while-iteration" << std::endl;
      // Preparing a set of sockets to be checked for events
      fd_set readset;
      FD_ZERO(&readset);
      FD_SET(listener_socket_holder_->GetSocket(), &readset);
      if (select(max_socket_to_check + 1, &readset, NULL, NULL, &timeout)
          > 0) {
//        std::cout << "  non-main: " << "gonna receive"
//            << std::endl;
        char buf[1024];
        int res = recv(listener_socket_holder_->GetSocket(), buf, sizeof(buf), 0);
//        std::cout << "  non-main: " << "received" << std::endl;
        if (res == 0)
          return false;
        if (res < 0) {
          return false;
        }
        if (handler.ProcessReceivedData(buf, res))
          return true;
      }
    }

    Send("BYE\0", 4);
    return true;
  }
};
