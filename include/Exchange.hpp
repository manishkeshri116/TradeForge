#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Order.hpp"
#include "OrderBook.hpp"
#include "TradeRepository.hpp"

class Exchange {
public:
Exchange() = default;

explicit Exchange(TradeRepository& repo)
    : repository(&repo)
{
    restore_from_repository();
}
void restore_from_repository()
{
    if (repository == nullptr) return;
    const auto orders = repository->load_latest_snapshot();
    for (const auto& order : orders)
    {
        auto [it, inserted] = order_books.try_emplace(order.symbol);
        if (!it->second.restore_order(order))
            throw std::runtime_error("Invalid or duplicate order in snapshot");
    }
}
void save_snapshot() const
{
    if (repository == nullptr) return;
    std::vector<Order> orders;
    for (const auto& [symbol, book] : order_books)
    {
        auto active = book.snapshot_orders();
        orders.insert(orders.end(), active.begin(), active.end());
    }
    std::stable_sort(orders.begin(), orders.end(), [](const Order& a, const Order& b) {
        return a.timestamp < b.timestamp;
    });
    repository->save_snapshot(orders);
}
bool add_order(const Order& order)
{
    if (order.order_id == 0 ||
        order.symbol.empty() ||
        (order.type == OrderType::LIMIT && order.price_ticks <= 0) ||
        order.quantity == 0)
    {
        return false;
    }

    auto& book = order_books[order.symbol];

    // Track trades before matching
    const auto previous_trade_count = book.get_trades().size();

    bool accepted = book.add_order(order);

    if (!accepted)
    {
        return false;
    }

    // Persist only newly executed trades
    if (repository != nullptr)
    {
        const auto& trades = book.get_trades();

        if (trades.size() > previous_trade_count)
        {
            std::vector<Trade> new_trades(
                trades.begin() + previous_trade_count,
                trades.end()
            );

            repository->save_trades(new_trades);
        }
        save_snapshot();
    }

    return true;
}
bool get_order_status(
    const std::string& symbol,
    std::uint64_t order_id,
    OrderStatus& status
) const
{
    auto it = order_books.find(symbol);

    if (it == order_books.end())
        return false;

    return it->second.get_order_status(order_id, status);
}
std::int64_t best_bid(const std::string& symbol) const {
    auto it = order_books.find(symbol);

    if (it == order_books.end()) {
        return 0;
    }

    return it->second.best_bid();
}

std::int64_t best_ask(const std::string& symbol) const {
    auto it = order_books.find(symbol);

    if (it == order_books.end()) {
        return 0;
    }

    return it->second.best_ask();
}
std::size_t get_order_count(const std::string& symbol) const {
    auto it = order_books.find(symbol);

    if (it == order_books.end()) {
        return 0;
    }

    return it->second.total_orders();
}
bool has_symbol(const std::string& symbol) const {
    return order_books.find(symbol) != order_books.end();
}
std::vector<Order> get_active_orders(const std::string& symbol) const {
    auto it = order_books.find(symbol);
    if (it == order_books.end()) return {};
    return it->second.snapshot_orders();
}
bool cancel_order(const std::string& symbol, std::uint64_t order_id) {
    auto it = order_books.find(symbol);

    if (it == order_books.end()) {
        return false;
    }

    const bool cancelled = it->second.cancel_order(order_id);
    if (cancelled && repository != nullptr) save_snapshot();
    return cancelled;
}
bool has_order(const std::string& symbol, std::uint64_t order_id) const {
    auto it = order_books.find(symbol);

    if (it == order_books.end()) {
        return false;
    }

    return it->second.has_order(order_id);
}
std::uint64_t remaining_quantity(
    const std::string& symbol,
    std::uint64_t order_id
) const {
    auto it = order_books.find(symbol);

    if (it == order_books.end()) {
        return 0;
    }

    return it->second.remaining_quantity(order_id);
}
const std::vector<Trade>& get_trades(const std::string& symbol) const {
    static const std::vector<Trade> empty_trades;

    auto it = order_books.find(symbol);

    if (it == order_books.end()) {
        return empty_trades;
    }

    return it->second.get_trades();
}
private:
    std::map<std::string, OrderBook> order_books;
    TradeRepository* repository = nullptr;
};
