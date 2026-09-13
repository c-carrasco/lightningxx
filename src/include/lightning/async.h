// MIT License - Copyright (c) 2026 Carlos Carrasco
#ifndef LIGHTNING_ASYNC_H
#define LIGHTNING_ASYNC_H
#include <functional>
#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/this_coro.hpp>
#include <lightning/http_request.h>
#include <lightning/http_response.h>

namespace lightning {
// Await Asio operations on the request executor using asio::use_awaitable.
template<class T = void>
using Task = asio::awaitable<T>;
using AsyncRequestHandler = std::function<Task<> (HttpRequest &, HttpResponse &)>;
}
#endif
