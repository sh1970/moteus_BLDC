// Copyright 2023 mjbots Robotic Systems, LLC.  info@mjbots.com
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>
#include <optional>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/redirect_error.hpp>

#include "mjlib/io/deadline_timer.h"

namespace moteus {
namespace tool {

/// Run the coroutine @f, but call 'cancel' on @p object if it does
/// not return before @p expires_at.
template <typename Object, typename Functor>
auto RunFor(const boost::asio::any_io_executor& executor,
            Object& object,
            Functor f,
            boost::posix_time::ptime expires_at)
    -> boost::asio::awaitable<std::optional<typename decltype(f())::value_type>> {

  struct Context {
    bool done = false;
    std::optional<boost::asio::deadline_timer> timer;
  };

  auto ctx = std::make_shared<Context>();
  ctx->timer.emplace(executor);
  ctx->timer->expires_at(expires_at);

  boost::asio::co_spawn(
      executor,
      [ctx, &object]() -> boost::asio::awaitable<void> {
        boost::system::error_code ec;
        co_await ctx->timer->async_wait(
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ctx->done) { co_return; }

        ctx->done = true;
        object.cancel();
      },
      boost::asio::detached);

  // Ensure that whether @f returns normally or throws, we mark the
  // watchdog as done and cancel its timer so it cannot fire a stale
  // cancel() on @object after we return.
  struct Guard {
    std::shared_ptr<Context> ctx;
    ~Guard() {
      ctx->done = true;
      boost::system::error_code ec;
      ctx->timer->cancel(ec);
    }
  };
  Guard guard{ctx};

  co_return co_await f();
}

}
}
