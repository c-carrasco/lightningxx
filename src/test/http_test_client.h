// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_HTTP_TEST_CLIENT_H
#define LIGHTNING_HTTP_TEST_CLIENT_H
#include <chrono>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <asio.hpp>

namespace lightning::test {
using namespace std::chrono_literals;

struct Response {
  int status;
  std::map<std::string, std::string> headers;
  std::string body;
};

// Raw TCP keeps framing and pipelining under test control. Every operation has
// a deadline so a server regression fails instead of hanging the test process.
class Client {
  public:
    explicit Client (uint16_t port, const std::string &address = "127.0.0.1"): socket { io } {
      socket.connect ({ asio::ip::make_address (address), port });
      socket.non_blocking (true);
    }
    void close() {
      std::error_code ignored;
      socket.close (ignored);
    }
    void finishSending() { socket.shutdown (asio::ip::tcp::socket::shutdown_send); }
    void send (std::string_view bytes) {
      const auto deadline = std::chrono::steady_clock::now() + 5s;
      while (!bytes.empty()) {
        std::error_code ec;
        const auto sent = socket.write_some (asio::buffer (bytes.data(), bytes.size()), ec);
        if (ec && !wouldBlock (ec))
          throw std::system_error (ec);
        bytes.remove_prefix (sent);
        if (std::chrono::steady_clock::now() >= deadline)
          throw std::runtime_error ("Timed out sending request");
        if (!sent) std::this_thread::sleep_for (1ms);
      }
    }
    Response readHeaders() {
      const auto deadline = std::chrono::steady_clock::now() + 5s;
      size_t end;
      while ((end = pending.find ("\r\n\r\n")) == std::string::npos)
        readMore (deadline);
      Response response { std::stoi (pending.substr (9, 3)), {}, {} };
      size_t pos = pending.find ("\r\n") + 2;
      while (pos < end) {
        const auto next = pending.find ("\r\n", pos);
        const auto colon = pending.find (':', pos);
        response.headers.emplace (pending.substr (pos, colon - pos), pending.substr (colon + 2, next - colon - 2));
        pos = next + 2;
      }
      pending.erase (0, end + 4);
      return response;
    }
    Response read (bool head = false) {
      auto response = readHeaders();
      const auto length = head || response.status == 204 || response.status == 304 ? 0 :
        std::stoul (response.headers.at ("content-length"));
      const auto deadline = std::chrono::steady_clock::now() + 5s;
      while (pending.size() < length)
        readMore (deadline);
      response.body = pending.substr (0, length);
      pending.erase (0, length);
      return response;
    }
    bool hasDataWithin (std::chrono::milliseconds duration) {
      if (!pending.empty()) return true;
      const auto deadline = std::chrono::steady_clock::now() + duration;
      do {
        std::error_code ec;
        if (socket.available (ec)) return true;
        if (ec) throw std::system_error (ec);
        std::this_thread::sleep_for (1ms);
      } while (std::chrono::steady_clock::now() < deadline);
      return false;
    }
    bool waitForClose() {
      const auto deadline = std::chrono::steady_clock::now() + 1s;
      do {
        char data[65536];
        std::error_code ec;
        const auto count = socket.read_some (asio::buffer (data), ec);
        if (ec == asio::error::eof || ec == asio::error::connection_reset) return true;
        if (ec && !wouldBlock (ec)) throw std::system_error (ec);
        pending.append (data, count);
        if (!count) std::this_thread::sleep_for (1ms);
      } while (std::chrono::steady_clock::now() < deadline);
      return false;
    }
  private:
    static bool wouldBlock (const std::error_code &ec) {
      return ec == asio::error::would_block || ec == asio::error::try_again;
    }
    void readMore (std::chrono::steady_clock::time_point deadline) {
      char data[65536];
      std::error_code ec;
      const auto count = socket.read_some (asio::buffer (data), ec);
      if (ec && !wouldBlock (ec)) throw std::system_error (ec);
      pending.append (data, count);
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error ("Timed out reading response");
      if (!count) std::this_thread::sleep_for (1ms);
    }
    asio::io_context io;
    asio::ip::tcp::socket socket;
    std::string pending;
};

}
#endif
