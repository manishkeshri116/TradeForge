#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "Trade.hpp"

struct Candlestick
{
    std::chrono::system_clock::time_point start_time;
    std::int64_t open_ticks;
    std::int64_t high_ticks;
    std::int64_t low_ticks;
    std::int64_t close_ticks;
    std::uint64_t volume;
    std::size_t trade_count;
};

struct PortfolioPosition
{
    std::string symbol;
    std::int64_t net_quantity;
    double average_entry_ticks;
    std::int64_t last_trade_price_ticks;
    double position_value;
    double realized_pnl;
    double unrealized_pnl;
};

inline std::vector<Candlestick> build_candlesticks(
    const std::vector<TradeRecord>& records,
    const std::string& symbol,
    std::chrono::milliseconds interval)
{
    if (interval.count() <= 0)
        throw std::invalid_argument("Candlestick interval must be positive");

    std::vector<const TradeRecord*> ordered;
    for (const auto& record : records) {
        if (record.symbol == symbol && record.trade.executed_at.has_value())
            ordered.push_back(&record);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        if (*left->trade.executed_at != *right->trade.executed_at)
            return *left->trade.executed_at < *right->trade.executed_at;
        return left->id < right->id;
    });

    std::vector<Candlestick> candles;
    const auto interval_us = std::chrono::duration_cast<std::chrono::microseconds>(interval).count();
    for (const auto* record : ordered) {
        const auto execution_us = std::chrono::duration_cast<std::chrono::microseconds>(
            record->trade.executed_at->time_since_epoch()).count();
        const auto bucket_us = execution_us - (execution_us % interval_us);
        const auto bucket = std::chrono::system_clock::time_point{
            std::chrono::microseconds(bucket_us)};
        if (candles.empty() || candles.back().start_time != bucket) {
            candles.push_back({bucket, record->trade.price_ticks, record->trade.price_ticks,
                               record->trade.price_ticks, record->trade.price_ticks,
                               record->trade.quantity, 1});
            continue;
        }

        auto& candle = candles.back();
        candle.high_ticks = std::max(candle.high_ticks, record->trade.price_ticks);
        candle.low_ticks = std::min(candle.low_ticks, record->trade.price_ticks);
        candle.close_ticks = record->trade.price_ticks;
        if (record->trade.quantity > std::numeric_limits<std::uint64_t>::max() - candle.volume)
            throw std::overflow_error("Candlestick volume exceeds supported range");
        candle.volume += record->trade.quantity;
        ++candle.trade_count;
    }
    return candles;
}

inline std::vector<PortfolioPosition> calculate_portfolio(
    const std::vector<TradeRecord>& records)
{
    // Portfolio exposure belongs to orders explicitly marked by the terminal. Other
    // execution legs supply reference prices but are not treated as account holdings.
    // Realized P&L uses weighted-average cost basis and excludes fees.
    struct RunningPosition
    {
        long double quantity{0};
        long double average_ticks{0};
        long double realized_ticks{0};
        std::int64_t last_price_ticks{0};
    };
    std::map<std::string, RunningPosition> positions;
    std::map<std::string, std::int64_t> last_trade_prices;
    std::vector<const TradeRecord*> ordered;
    ordered.reserve(records.size());
    for (const auto& record : records) ordered.push_back(&record);
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        return left->id < right->id;
    });

    constexpr auto max_quantity = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    for (const auto* record : ordered) {
        if (record->symbol.empty()) continue;
        if (record->trade.quantity > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            throw std::overflow_error("Trade quantity exceeds portfolio range");
        last_trade_prices[record->symbol] = record->trade.price_ticks;
        const long double quantity = static_cast<long double>(record->trade.quantity);
        const auto price = static_cast<long double>(record->trade.price_ticks);
        auto apply_owned_leg = [&](bool is_buy) {
            auto& position = positions[record->symbol];
            const long double delta = is_buy ? quantity : -quantity;
            const long double old_quantity = position.quantity;
            const long double new_quantity = old_quantity + delta;
            if (old_quantity == 0 || (old_quantity > 0) == (delta > 0)) {
                const long double old_cost = std::abs(old_quantity) * position.average_ticks;
                position.average_ticks = (old_cost + quantity * price) / std::abs(new_quantity);
            } else {
                const long double closed_quantity = std::min(std::abs(old_quantity), quantity);
                const long double pnl_ticks = old_quantity > 0
                    ? price - position.average_ticks
                    : position.average_ticks - price;
                position.realized_ticks += closed_quantity * pnl_ticks;
                if (new_quantity == 0) {
                    position.average_ticks = 0;
                } else if ((new_quantity > 0) != (old_quantity > 0)) {
                    position.average_ticks = price;
                }
            }
            if (std::abs(new_quantity) > max_quantity)
                throw std::overflow_error("Portfolio position exceeds supported range");
            position.quantity = new_quantity;
        };
        if (record->trade.buy_order_owned) apply_owned_leg(true);
        if (record->trade.sell_order_owned) apply_owned_leg(false);
    }

    std::vector<PortfolioPosition> result;
    result.reserve(positions.size());
    for (const auto& [symbol, position] : positions) {
        const auto last_price = last_trade_prices.find(symbol);
        if (last_price == last_trade_prices.end()) continue;
        const auto quantity = static_cast<std::int64_t>(position.quantity);
        const long double unrealized_ticks = position.quantity > 0
            ? (last_price->second - position.average_ticks) * position.quantity
            : position.quantity < 0
                ? (position.average_ticks - last_price->second) * std::abs(position.quantity)
                : 0;
        result.push_back({symbol, quantity, static_cast<double>(position.average_ticks),
                          last_price->second,
                          static_cast<double>(position.quantity * last_price->second / 100.0L),
                          static_cast<double>(position.realized_ticks / 100.0L),
                          static_cast<double>(unrealized_ticks / 100.0L)});
    }
    return result;
}
