// MIT License - Copyright (c) 2026 Carlos Carrasco
// Standalone diagnostic: intentionally links no Lightning++ code.
#include <chrono>
#include <asio.hpp>
#include <array>
#include <functional>
#include <memory>
#include <thread>
void once() {
  asio::io_context server;
  auto guard = asio::make_work_guard(server);
  asio::ip::tcp::acceptor acceptor(asio::make_strand(server));
  acceptor.open(asio::ip::tcp::v4());
  acceptor.bind({asio::ip::address_v4::loopback(), 0});
  acceptor.listen();
  const auto port = acceptor.local_endpoint().port();
  std::function<void()> accept;
  accept = [&] {
    acceptor.async_accept(asio::make_strand(server), [&](std::error_code ec, asio::ip::tcp::socket socket) {
      if (ec) return;
      auto peer = std::make_shared<asio::ip::tcp::socket>(std::move(socket));
      auto byte = std::make_shared<std::array<char, 1>>();
      asio::async_read(*peer, asio::buffer(*byte), [peer, byte](std::error_code error, size_t) {
        if (!error) asio::async_write(*peer, asio::buffer(*byte), [peer, byte](std::error_code, size_t) {});
      });
      accept();
    });
  };
  accept();
  std::thread first([&] { server.run(); });
  std::thread second([&] { server.run(); });
  std::thread third([&] { server.run(); });
  asio::io_context clients;
  for (int i = 0; i < 2; ++i) {
    asio::ip::tcp::socket client(clients);
    client.connect({asio::ip::address_v4::loopback(), port});
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::array<char, 1> byte{'x'};
    asio::write(client, asio::buffer(byte));
    asio::read(client, asio::buffer(byte));
  }
  server.stop();
  first.join(); second.join(); third.join();
}

int main() { for (int i = 0; i < 10; ++i) once(); }
