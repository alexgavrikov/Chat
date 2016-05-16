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
    for (; sz > 0; --sz, ++data) {
      std::cout << *data;
    }
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
  TSocketPtr socket_holder_;

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
      : socket_holder_(new TSocketHolder) {
  }
  TSocket(int sock)
      : socket_holder_(new TSocketHolder(sock)) {
  }

  int GetSocket() const {
    return socket_holder_->GetSocket();
  }

  void Connect(const std::string &host, int port) {
    int addr;
    if (!ResolveHost(host, addr))
      throw std::runtime_error("can't resolve host");
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = addr;
    if (connect(socket_holder_->GetSocket(), reinterpret_cast<sockaddr*>(&address), sizeof(address))
        < 0)
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
    }
    if (bind(socket_holder_->GetSocket(), reinterpret_cast<sockaddr*>(&address), sizeof(address))
        < 0)
      throw std::runtime_error("can't bind");
    if (listen(socket_holder_->GetSocket(), 1) < 0)
      throw std::runtime_error("can't start listening");
  }

  // Old (original) AcceptLoop
  template<typename TAcceptHandler>
  void AcceptLoop(TAcceptHandler &handler) const {
    for (;;) {
      int sock = accept(socket_holder_->GetSocket(), nullptr, nullptr);
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
    fcntl(socket_holder_->GetSocket(), F_SETFL, O_NONBLOCK);
    // Let's use 'select' function. We sleep on it
    // until events happen in sockets or streams.
    // Timeout prevents infinite sleeping.

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
      // Preparing a set of sockets to be checked for events
      int max_socket_to_check = socket_holder_->GetSocket();
      timeval timeout;
      timeout.tv_sec = 15;
      timeout.tv_usec = 0;
      fd_set readset;
      FD_ZERO(&readset);
      FD_SET(socket_holder_->GetSocket(), &readset);
      if (select(max_socket_to_check + 1, &readset, NULL, NULL, &timeout) <= 0) {
        if (threads_pool.UnfinishedTasksCount() < kMinimalClientsCount) {
          // Only one client in chat. Our chat doesn't make any sense.
          std::cout << "main: " << "Too few clients => break" << std::endl;
          break;
        }
      } else {
        int sock = accept(socket_holder_->GetSocket(), nullptr, nullptr);
        if (sock < 0) {
          std::cout << "can't bind" << std::endl;
        } else {
          TSocket res(sock);
          auto future = threads_pool.enqueue([res, &handler, &time_to_stop_all_threads] () {
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
      int res = send(socket_holder_->GetSocket(), data, sz, 0);
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
  bool RecvLoop(TIOHandler &handler, const std::atomic<bool>& time_to_stop) const {
    // I don't want client-socket to block during recv
    fcntl(socket_holder_->GetSocket(), F_SETFL, O_NONBLOCK);
    // Let's use 'select' function. We sleep in it
    // until events happen in sockets or streams.
    // Timeout is for preventing infinite sleeping.

    while (!time_to_stop) {
      // Preparing a set of sockets to be checked for events
      int max_socket_to_check = socket_holder_->GetSocket();
      timeval timeout;
      timeout.tv_sec = 5;
      timeout.tv_usec = 0;
      fd_set readset;
      FD_ZERO(&readset);
      FD_SET(socket_holder_->GetSocket(), &readset);
      if (select(max_socket_to_check + 1, &readset, NULL, NULL, &timeout) > 0) {
        char buf[1024];
        int res = recv(socket_holder_->GetSocket(), buf, sizeof(buf), 0);
        if (res == 0)
          return false;
        if (res < 0) {
          return false;
        }
        if (handler.ProcessReceivedData(buf, res))
          return true;
      }
    }

    Send("BYE", 3);
    return true;
  }
};
