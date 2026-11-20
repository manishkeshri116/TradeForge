#pragma once

#include <chrono>
#include <cstdint>
#include <string>

enum class Side
{
    BUY,
    SELL
};
enum class OrderType
{
    LIMIT,
    MARKET
};
enum class OrderStatus
{
    OPEN,
    PARTIALLY_FILLED,
    FILLED,
    CANCELLED
};
struct Order
{
    std::uint64_t order_id;
    std::string symbol;
    Side side;
    std::int64_t price_ticks;
    std::uint64_t quantity;
    OrderType type{OrderType::LIMIT};
    OrderStatus status{OrderStatus::OPEN};
    std::chrono::steady_clock::time_point timestamp{
        std::chrono::steady_clock::now()};
    bool portfolio_owned{false};
};

inline double get_price(const Order &order)
{
    return static_cast<double>(order.price_ticks) / 100.0;
}

inline std::string get_side_name(Side side)
{
    switch (side)
    {
    case Side::BUY:
        return "BUY";
    case Side::SELL:
        return "SELL";
    }

    return "UNKNOWN";
}
inline std::string get_status_name(OrderStatus status)
{
    switch (status)
    {
        case OrderStatus::OPEN:
            return "OPEN";

        case OrderStatus::PARTIALLY_FILLED:
            return "PARTIALLY_FILLED";

        case OrderStatus::FILLED:
            return "FILLED";

        case OrderStatus::CANCELLED:
            return "CANCELLED";
    }

    return "UNKNOWN";
}
