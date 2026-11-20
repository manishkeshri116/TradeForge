#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <unordered_map>
#include <vector>

#include "Order.hpp"
#include "Trade.hpp"

class OrderBook
{
public:
    std::vector<Order> snapshot_orders() const
    {
        std::vector<Order> result;
        for (const auto &[price, orders] : buy_orders)
            result.insert(result.end(), orders.begin(), orders.end());
        for (const auto &[price, orders] : sell_orders)
            result.insert(result.end(), orders.begin(), orders.end());
        std::stable_sort(result.begin(), result.end(), [](const Order &a, const Order &b) {
            return a.timestamp < b.timestamp;
        });
        return result;
    }

    bool restore_order(const Order &order)
    {
        if (order.order_id == 0 || order.symbol.empty() || order.quantity == 0 ||
            order.type != OrderType::LIMIT || order.price_ticks <= 0 ||
            (order.status != OrderStatus::OPEN && order.status != OrderStatus::PARTIALLY_FILLED) ||
            order_statuses.count(order.order_id) != 0)
            return false;
        order_statuses[order.order_id] = order.status;
        if (order.side == Side::BUY)
            buy_orders[order.price_ticks].push_back(order);
        else
            sell_orders[order.price_ticks].push_back(order);
        ++active_order_count;
        return true;
    }

    bool add_order(const Order &order)
    {
        if (order.order_id == 0 ||
            order.symbol.empty() ||
            order.quantity == 0 ||
            (order.type == OrderType::LIMIT && order.price_ticks <= 0))
        {
            return false;
        }

        // Reject duplicate order IDs
        if (order_statuses.find(order.order_id) != order_statuses.end())
        {
            return false;
        }

        order_statuses[order.order_id] = OrderStatus::OPEN;

        if (order.type == OrderType::MARKET)
        {
            execute_market_order(order);
            return true;
        }

        if (order.side == Side::BUY)
        {
            buy_orders[order.price_ticks].push_back(order);
        }
        else
        {
            sell_orders[order.price_ticks].push_back(order);
        }

        ++active_order_count;
        match_orders();

        return true;
    }

    std::size_t total_orders() const
    {
        return active_order_count;
    }

    std::int64_t best_bid() const
    {
        if (buy_orders.empty())
        {
            return 0;
        }

        return buy_orders.begin()->first;
    }
    std::int64_t best_ask() const
    {
        if (sell_orders.empty())
        {
            return 0;
        }

        return sell_orders.begin()->first;
    }
    double get_bid_ask_spread() const
    {
        if (buy_orders.empty() || sell_orders.empty())
        {
            return 0.0;
        }

        return static_cast<double>(
                   sell_orders.begin()->first - buy_orders.begin()->first) /
               100.0;
    }
    bool is_empty() const
    {
        return buy_orders.empty() && sell_orders.empty();
    }
    bool has_match() const
    {
        if (buy_orders.empty() || sell_orders.empty())
        {
            return false;
        }

        return best_bid() >= best_ask();
    }
    const std::vector<Trade> &get_trades() const
    {
        return trades;
    }

    void display_order_book() const
    {
        std::cout << "\n========== ORDER BOOK ==========\n";

        // ASK SIDE
        std::cout << "\n             ASKS\n";
        std::cout << "Price       Order ID       Quantity\n";

        for (const auto &[price, orders] : sell_orders)
        {
            for (const auto &order : orders)
            {
                std::cout << "₹" << std::fixed << std::setprecision(2)
                          << static_cast<double>(price) / 100.0
                          << "       " << order.order_id
                          << "             " << order.quantity << '\n';
            }
        }

        // BID SIDE
        std::cout << "\n             BIDS\n";
        std::cout << "Price       Order ID       Quantity\n";

        for (const auto &[price, orders] : buy_orders)
        {
            for (const auto &order : orders)
            {
                std::cout << "₹" << std::fixed << std::setprecision(2)
                          << static_cast<double>(price) / 100.0
                          << "       " << order.order_id
                          << "             " << order.quantity << '\n';
            }
        }

        std::cout << "================================\n";
    }

    void display_market_depth() const
    {
        std::cout << "\n========== MARKET DEPTH ==========\n";

        std::cout << "\n             ASKS\n";
        std::cout << "Price       Orders       Quantity\n";

        for (const auto &[price, orders] : sell_orders)
        {
            std::uint64_t total_quantity = 0;

            for (const auto &order : orders)
            {
                total_quantity += order.quantity;
            }

            std::cout << "₹" << std::fixed << std::setprecision(2)
                      << static_cast<double>(price) / 100.0
                      << "       " << orders.size()
                      << "            " << total_quantity << '\n';
        }

        std::cout << "\n             BIDS\n";
        std::cout << "Price       Orders       Quantity\n";

        for (const auto &[price, orders] : buy_orders)
        {
            std::uint64_t total_quantity = 0;

            for (const auto &order : orders)
            {
                total_quantity += order.quantity;
            }

            std::cout << "₹" << std::fixed << std::setprecision(2)
                      << static_cast<double>(price) / 100.0
                      << "       " << orders.size()
                      << "            " << total_quantity << '\n';
        }

        std::cout << "==================================\n";
    }
    void match_orders()
    {
        while (has_match())
        {
            auto buy_it = buy_orders.begin();
            auto sell_it = sell_orders.begin();

            Order &buy = buy_it->second.front();
            Order &sell = sell_it->second.front();

            std::uint64_t executed_quantity =
                std::min(buy.quantity, sell.quantity);

            // Older order determines the execution price.
            std::int64_t execution_price =
                (buy.timestamp < sell.timestamp)
                    ? buy.price_ticks
                    : sell.price_ticks;

            trades.push_back({buy.order_id,
                              sell.order_id,
                              execution_price,
                              executed_quantity,
                              buy.symbol,
                              std::chrono::system_clock::now(),
                              buy.portfolio_owned,
                              sell.portfolio_owned});

            buy.quantity -= executed_quantity;
            sell.quantity -= executed_quantity;
            buy.status = (buy.quantity == 0)
                             ? OrderStatus::FILLED
                             : OrderStatus::PARTIALLY_FILLED;

            sell.status = (sell.quantity == 0)
                              ? OrderStatus::FILLED
                              : OrderStatus::PARTIALLY_FILLED;
            order_statuses[buy.order_id] = buy.status;
            order_statuses[sell.order_id] = sell.status;

            if (buy.quantity == 0)
            {
                buy_it->second.pop_front();
                --active_order_count;

                if (buy_it->second.empty())
                {
                    buy_orders.erase(buy_it);
                }
            }

            if (sell.quantity == 0)
            {
                sell_it->second.pop_front();
                --active_order_count;
                if (sell_it->second.empty())
                {
                    sell_orders.erase(sell_it);
                }
            }
        }
    }
    std::uint64_t remaining_quantity(std::uint64_t order_id) const
    {
        for (const auto &[price, orders] : buy_orders)
        {
            for (const auto &order : orders)
            {
                if (order.order_id == order_id)
                {
                    return order.quantity;
                }
            }
        }

        for (const auto &[price, orders] : sell_orders)
        {
            for (const auto &order : orders)
            {
                if (order.order_id == order_id)
                {
                    return order.quantity;
                }
            }
        }

        return 0;
    }
    bool has_order(std::uint64_t order_id) const
    {
        for (const auto &[price, orders] : buy_orders)
        {
            for (const auto &order : orders)
            {
                if (order.order_id == order_id)
                {
                    return true;
                }
            }
        }

        for (const auto &[price, orders] : sell_orders)
        {
            for (const auto &order : orders)
            {
                if (order.order_id == order_id)
                {
                    return true;
                }
            }
        }

        return false;
    }
    bool get_order_status(
        std::uint64_t order_id,
        OrderStatus &status) const
    {
        auto it = order_statuses.find(order_id);

        if (it == order_statuses.end())
            return false;

        status = it->second;
        return true;
    }
    bool cancel_order(std::uint64_t order_id)
    {
        for (auto &[price, orders] : buy_orders)
        {
            auto it = std::find_if(
                orders.begin(), orders.end(),
                [order_id](const Order &order)
                {
                    return order.order_id == order_id;
                });

            if (it != orders.end())
            {
                orders.erase(it);
                --active_order_count;
                order_statuses[order_id] = OrderStatus::CANCELLED;
                if (orders.empty())
                {
                    buy_orders.erase(price);
                }

                return true;
            }
        }

        for (auto &[price, orders] : sell_orders)
        {
            auto it = std::find_if(
                orders.begin(), orders.end(),
                [order_id](const Order &order)
                {
                    return order.order_id == order_id;
                });

            if (it != orders.end())
            {
                orders.erase(it);

                --active_order_count;
                order_statuses[order_id] = OrderStatus::CANCELLED;
                if (orders.empty())
                {
                    sell_orders.erase(price);
                }

                return true;
            }
        }

        return false;
    }
    bool modify_order(
        std::uint64_t order_id,
        std::int64_t new_price,
        std::uint64_t new_quantity)
    {
        if (new_price <= 0 || new_quantity == 0)
        {
            return false;
        }

        Order modified_order;

        bool found = false;

        for (const auto &[price, orders] : buy_orders)
        {
            auto it = std::find_if(
                orders.begin(),
                orders.end(),
                [order_id](const Order &order)
                {
                    return order.order_id == order_id;
                });

            if (it != orders.end())
            {
                modified_order = *it;
                found = true;
                break;
            }
        }

        if (!found)
        {
            for (const auto &[price, orders] : sell_orders)
            {
                auto it = std::find_if(
                    orders.begin(),
                    orders.end(),
                    [order_id](const Order &order)
                    {
                        return order.order_id == order_id;
                    });

                if (it != orders.end())
                {
                    modified_order = *it;
                    found = true;
                    break;
                }
            }
        }

        if (!found)
        {
            return false;
        }

        if (!cancel_order(order_id))
        {
            return false;
        }
        order_statuses.erase(order_id);

        modified_order.price_ticks = new_price;
        modified_order.quantity = new_quantity;
        modified_order.timestamp = std::chrono::steady_clock::now();
        modified_order.status = OrderStatus::OPEN;

        return add_order(modified_order);
    }
    void execute_market_order(Order market_order)
    {
        const std::uint64_t original_quantity = market_order.quantity;

        while (market_order.quantity > 0)
        {
            if (market_order.side == Side::BUY)
            {
                if (sell_orders.empty())
                    break;

                auto it = sell_orders.begin();
                Order &resting_order = it->second.front();

                std::uint64_t executed = std::min(
                    market_order.quantity,
                    resting_order.quantity);

                trades.push_back({market_order.order_id,
                                  resting_order.order_id,
                                  resting_order.price_ticks,
                                  executed,
                                  market_order.symbol,
                                  std::chrono::system_clock::now(),
                                  market_order.portfolio_owned,
                                  resting_order.portfolio_owned});

                market_order.quantity -= executed;
                resting_order.quantity -= executed;

                resting_order.status = (resting_order.quantity == 0)
                                           ? OrderStatus::FILLED
                                           : OrderStatus::PARTIALLY_FILLED;

                order_statuses[resting_order.order_id] = resting_order.status;

                if (resting_order.quantity == 0)
                {
                    it->second.pop_front();
                    --active_order_count;

                    if (it->second.empty())
                        sell_orders.erase(it);
                }
            }
            else
            {
                if (buy_orders.empty())
                    break;

                auto it = buy_orders.begin();
                Order &resting_order = it->second.front();

                std::uint64_t executed = std::min(
                    market_order.quantity,
                    resting_order.quantity);

                trades.push_back({resting_order.order_id,
                                  market_order.order_id,
                                  resting_order.price_ticks,
                                  executed,
                                  market_order.symbol,
                                  std::chrono::system_clock::now(),
                                  resting_order.portfolio_owned,
                                  market_order.portfolio_owned});

                market_order.quantity -= executed;
                resting_order.quantity -= executed;

                resting_order.status = (resting_order.quantity == 0)
                                           ? OrderStatus::FILLED
                                           : OrderStatus::PARTIALLY_FILLED;

                order_statuses[resting_order.order_id] = resting_order.status;

                if (resting_order.quantity == 0)
                {
                    it->second.pop_front();
                    --active_order_count;

                    if (it->second.empty())
                        buy_orders.erase(it);
                }
            }
        }

        if (market_order.quantity == 0)
        {
            order_statuses[market_order.order_id] = OrderStatus::FILLED;
        }
        else if (market_order.quantity < original_quantity)
        {
            order_statuses[market_order.order_id] = OrderStatus::PARTIALLY_FILLED;
        }
        else
        {
            order_statuses[market_order.order_id] = OrderStatus::CANCELLED;
        }
    }

private:
    std::vector<Trade> trades;
    std::map<std::int64_t, std::deque<Order>, std::greater<>> buy_orders;
    std::map<std::int64_t, std::deque<Order>> sell_orders;
    std::size_t active_order_count = 0;
    std::unordered_map<std::uint64_t, OrderStatus> order_statuses;
};
