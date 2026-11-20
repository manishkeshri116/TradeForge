#pragma once

#include <cstdint>
#include <chrono>
#include <optional>
#include <string>

struct Trade {
    std::uint64_t buy_order_id;
    std::uint64_t sell_order_id;
    std::int64_t price_ticks;
    std::uint64_t quantity;
    std::string symbol{};
    std::optional<std::chrono::system_clock::time_point> executed_at{std::nullopt};
    bool buy_order_owned{false};
    bool sell_order_owned{false};
};

struct TradeRecord {
    std::int64_t id;
    std::string symbol;
    Trade trade;
};
