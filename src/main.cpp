#include <iostream>
#include <iomanip>
#include "Exchange.hpp"
#include "Order.hpp"
#include "OrderBook.hpp"

int main()
{
    OrderBook book;

    Order buy_order{
        1001,
        "AAPL",
        Side::BUY,
        15050,
        10};

    Order sell_order{
        1002,
        "AAPL",
        Side::SELL,
        15100,
        5};
    Order buy_order_2{
        1003,
        "AAPL",
        Side::BUY,
        15200,
        20};

    book.add_order(buy_order);
    book.add_order(sell_order);
    book.add_order(buy_order_2);

    std::cout << "Total Orders: " << book.total_orders() << '\n';
    std::cout << "Best Bid: ₹"
              << static_cast<double>(book.best_bid()) / 100.0 << '\n';

    std::cout << "Best Ask: ₹"
              << static_cast<double>(book.best_ask()) / 100.0 << '\n';
    std::cout << "Match Available: "
              << (book.has_match() ? "YES" : "NO") << '\n';
    std::cout << "\n--- Executed Trades ---\n";

    for (const auto &trade : book.get_trades())
    {
        std::cout << "Buy Order: " << trade.buy_order_id << '\n';
        std::cout << "Sell Order: " << trade.sell_order_id << '\n';

        std::cout << "Execution Price: ₹"
                  << std::fixed << std::setprecision(2)
                  << static_cast<double>(trade.price_ticks) / 100.0
                  << '\n';

        std::cout << "Quantity: " << trade.quantity << '\n';
    }
    std::cout << "\n--- Remaining Orders ---\n";

    std::cout << "Order 1003 Remaining Quantity: "
              << book.remaining_quantity(1003) << '\n';

    std::cout << "Order 1002 Remaining Quantity: "
              << book.remaining_quantity(1002) << '\n';
    std::cout << "\n--- Order Cancellation ---\n";

    bool cancelled = book.cancel_order(1003);

    std::cout << "Order 1003 Cancelled: "
              << (cancelled ? "YES" : "NO") << '\n';

    std::cout << "Total Orders After Cancellation: "
              << book.total_orders() << '\n';

    std::cout << "\n--- Order Validation ---\n";

    Order invalid_order{
        1004,
        "AAPL",
        Side::BUY,
        15000,
        0};

    bool accepted = book.add_order(invalid_order);

    std::cout << "Invalid Order Accepted: "
              << (accepted ? "YES" : "NO") << '\n';
    std::cout << "\n--- Multi-Stock Exchange ---\n";

    TradeRepository repository("trading_engine.db");
Exchange exchange(repository);

    Order apple_order{
        2001, "AAPL", Side::BUY, 15000, 10};

    Order tesla_order{
        2002, "TSLA", Side::BUY, 25000, 20};

    std::cout << "AAPL Accepted: "
              << (exchange.add_order(apple_order) ? "YES" : "NO") << '\n';

    std::cout << "TSLA Accepted: "
              << (exchange.add_order(tesla_order) ? "YES" : "NO") << '\n';
    std::cout << "AAPL Best Bid: ₹"
              << static_cast<double>(exchange.best_bid("AAPL")) / 100.0
              << '\n';

    std::cout << "TSLA Best Bid: ₹"
              << static_cast<double>(exchange.best_bid("TSLA")) / 100.0
              << '\n';
    std::cout << "AAPL Best Ask: ₹"
              << static_cast<double>(exchange.best_ask("AAPL")) / 100.0
              << '\n';

    std::cout << "TSLA Best Ask: ₹"
              << static_cast<double>(exchange.best_ask("TSLA")) / 100.0
              << '\n';
    Order apple_sell_order{
        2003, "AAPL", Side::SELL, 15100, 5};
    std::cout << "TSLA Best Ask After AAPL Sell: ₹"
              << static_cast<double>(exchange.best_ask("TSLA")) / 100.0
              << '\n';

    std::cout << "AAPL Sell Accepted: "
              << (exchange.add_order(apple_sell_order) ? "YES" : "NO")
              << '\n';
    std::cout << "AAPL Updated Best Ask: ₹"
              << static_cast<double>(exchange.best_ask("AAPL")) / 100.0
              << '\n';

    std::cout << "AAPL Active Orders: "
              << exchange.get_order_count("AAPL") << '\n';

    std::cout << "TSLA Active Orders: "
              << exchange.get_order_count("TSLA") << '\n';
    std::cout << "AAPL Exists: "
              << (exchange.has_symbol("AAPL") ? "YES" : "NO") << '\n';

    std::cout << "GOOGL Exists: "
              << (exchange.has_symbol("GOOGL") ? "YES" : "NO") << '\n';

    Order invalid_exchange_order{
        0, "GOOGL", Side::BUY, 15000, 10};

    std::cout << "Invalid Exchange Order Accepted: "
              << (exchange.add_order(invalid_exchange_order) ? "YES" : "NO")
              << '\n';

    std::cout << "GOOGL Exists After Invalid Order: "
              << (exchange.has_symbol("GOOGL") ? "YES" : "NO")
              << '\n';

    std::cout << "AAPL Order 2001 Cancelled: "
              << (exchange.cancel_order("AAPL", 2001) ? "YES" : "NO")
              << '\n';

    std::cout << "AAPL Active Orders After Cancellation: "
              << exchange.get_order_count("AAPL") << '\n';

    std::cout << "GOOGL Order Cancelled: "
              << (exchange.cancel_order("GOOGL", 9999) ? "YES" : "NO")
              << '\n';
    std::cout << "AAPL Order 2003 Exists: "
              << (exchange.has_order("AAPL", 2003) ? "YES" : "NO")
              << '\n';

    std::cout << "AAPL Order 2001 Exists: "
              << (exchange.has_order("AAPL", 2001) ? "YES" : "NO")
              << '\n';

    std::cout << "TSLA Order 2002 Exists: "
              << (exchange.has_order("TSLA", 2002) ? "YES" : "NO")
              << '\n';
    Order apple_matching_buy{
        3001, "AAPL", Side::BUY, 15200, 10};

    Order apple_matching_sell{
        3002, "AAPL", Side::SELL, 15100, 5};

    exchange.add_order(apple_matching_buy);
    exchange.add_order(apple_matching_sell);

    std::cout << "AAPL Trades After Matching: "
              << exchange.get_trades("AAPL").size() << '\n';
    for (const auto &trade : exchange.get_trades("AAPL"))
    {
        std::cout << "Buy Order: " << trade.buy_order_id << '\n';
        std::cout << "Sell Order: " << trade.sell_order_id << '\n';

        std::cout << "Price: ₹"
                  << static_cast<double>(trade.price_ticks) / 100.0
                  << '\n';

        std::cout << "Quantity: " << trade.quantity << "\n\n";
    }

    std::cout << "TSLA Trades After AAPL Matching: "
              << exchange.get_trades("TSLA").size() << '\n';
    std::cout << "\n--- Remaining Quantity ---\n";

    std::cout << "Order 3001 Remaining: "
              << exchange.remaining_quantity("AAPL", 3001) << '\n';

    std::cout << "Order 3002 Remaining: "
              << exchange.remaining_quantity("AAPL", 3002) << '\n';
    std::cout << "\n--- Exchange Trade History ---\n";

    std::cout << "AAPL Trades: "
              << exchange.get_trades("AAPL").size() << '\n';

    std::cout << "TSLA Trades: "
              << exchange.get_trades("TSLA").size() << '\n';

    std::cout << "GOOGL Trades: "
              << exchange.get_trades("GOOGL").size() << '\n';

    std::cout << "\n--- Market Order Test ---\n";

    OrderBook market_book;

    Order limit_sell{
        4001, "MSFT", Side::SELL, 15500, 10};

    Order market_buy{
        4002, "MSFT", Side::BUY, 0, 6, OrderType::MARKET};

    market_book.add_order(limit_sell);
    market_book.add_order(market_buy);

    std::cout << "Executed Trades: "
              << market_book.get_trades().size() << '\n';

    std::cout << "Remaining Orders: "
              << market_book.total_orders() << '\n';

    std::cout << "Remaining Sell Quantity: "
              << market_book.remaining_quantity(4001) << '\n';
    std::cout << "\n--- Market SELL Test ---\n";

    OrderBook sell_market_book;

    Order limit_buy{
        5001, "GOOGL", Side::BUY, 15400, 8};

    Order market_sell{
        5002, "GOOGL", Side::SELL, 0, 5, OrderType::MARKET};

    sell_market_book.add_order(limit_buy);
    sell_market_book.add_order(market_sell);

    std::cout << "Executed Trades: "
              << sell_market_book.get_trades().size() << '\n';

    std::cout << "Remaining Buy Quantity: "
              << sell_market_book.remaining_quantity(5001) << '\n';

    std::cout << "\n--- Exchange Market Order Test ---\n";

    Exchange market_exchange;

    Order exchange_limit_sell{
        6001, "AAPL", Side::SELL, 15500, 10};

    Order exchange_market_buy{
        6002, "AAPL", Side::BUY, 0, 6, OrderType::MARKET};

    market_exchange.add_order(exchange_limit_sell);
    market_exchange.add_order(exchange_market_buy);

    std::cout << "AAPL Trades: "
              << market_exchange.get_trades("AAPL").size() << '\n';

    std::cout << "AAPL Remaining Orders: "
              << market_exchange.get_order_count("AAPL") << '\n';

    std::cout << "Remaining Sell Quantity: "
              << market_exchange.remaining_quantity("AAPL", 6001) << '\n';


    std::cout << "\n--- Order Status Test ---\n";

    OrderBook status_book;

    Order status_sell{
        9001, "AAPL", Side::SELL, 15000, 10};

    Order status_buy{
        9002, "AAPL", Side::BUY, 15000, 5};

    status_book.add_order(status_sell);
    status_book.add_order(status_buy);

    OrderStatus status;

    if (status_book.get_order_status(9001, status))
    {
        std::cout << "Sell Order Status: "
                  << get_status_name(status) << '\n';
    }

    if (status_book.get_order_status(9002, status))
    {
        std::cout << "Buy Order Status: "
                  << get_status_name(status) << '\n';
    }

    status_book.cancel_order(9001);

if (status_book.get_order_status(9001, status))
{
    std::cout << "Cancelled Order Status: "
              << get_status_name(status) << '\n';
}

std::cout << "\n--- Order Book Display Test ---\n";

OrderBook display_book;

display_book.add_order({
    9501, "AAPL", Side::BUY, 15000, 20
});

display_book.add_order({
    9502, "AAPL", Side::BUY, 15200, 15
});

display_book.add_order({
    9503, "AAPL", Side::SELL, 15500, 10
});

display_book.add_order({
    9504, "AAPL", Side::SELL, 15400, 25
});

display_book.display_order_book();


std::cout << "\n--- FIFO Priority Test ---\n";

OrderBook fifo_book;

fifo_book.add_order({9601, "AAPL", Side::SELL, 15000, 10});
fifo_book.add_order({9602, "AAPL", Side::SELL, 15000, 10});

fifo_book.add_order({9603, "AAPL", Side::BUY, 15000, 15});

for (const auto& trade : fifo_book.get_trades())
{
    std::cout << "Buy Order: " << trade.buy_order_id
              << " | Sell Order: " << trade.sell_order_id
              << " | Quantity: " << trade.quantity << '\n';
}

std::cout << "\n--- FIFO Partial Fill Test ---\n";

OrderBook partial_fifo_book;

partial_fifo_book.add_order({9701, "AAPL", Side::SELL, 15000, 5});
partial_fifo_book.add_order({9702, "AAPL", Side::SELL, 15000, 10});
partial_fifo_book.add_order({9703, "AAPL", Side::SELL, 15000, 15});

partial_fifo_book.add_order({9704, "AAPL", Side::BUY, 15000, 8});

for (const auto& trade : partial_fifo_book.get_trades())
{
    std::cout << "Buy Order: " << trade.buy_order_id
              << " | Sell Order: " << trade.sell_order_id
              << " | Quantity: " << trade.quantity << '\n';
}

std::cout << "Remaining Sell 9702: "
          << partial_fifo_book.remaining_quantity(9702) << '\n';

std::cout << "Remaining Sell 9703: "
          << partial_fifo_book.remaining_quantity(9703) << '\n';

          std::cout << "\n--- Market Depth Test ---\n";

OrderBook depth_book;

depth_book.add_order({9801, "AAPL", Side::SELL, 15400, 10});
depth_book.add_order({9802, "AAPL", Side::SELL, 15400, 15});
depth_book.add_order({9803, "AAPL", Side::SELL, 15500, 20});

depth_book.add_order({9804, "AAPL", Side::BUY, 15200, 12});
depth_book.add_order({9805, "AAPL", Side::BUY, 15200, 8});
depth_book.add_order({9806, "AAPL", Side::BUY, 15000, 25});

depth_book.display_market_depth();

std::cout << "\n--- Bid Ask Spread Test ---\n";

OrderBook spread_book;

spread_book.add_order({9901, "AAPL", Side::BUY, 15200, 10});
spread_book.add_order({9902, "AAPL", Side::SELL, 15400, 10});

std::cout << "Best Bid: ₹152.00\n";
std::cout << "Best Ask: ₹154.00\n";

std::cout << "Bid Ask Spread: ₹"
          << std::fixed << std::setprecision(2)
          << spread_book.get_bid_ask_spread() << '\n';

std::cout << "\n--- Order Modification Test ---\n";

OrderBook modify_book;

modify_book.add_order({10001, "AAPL", Side::BUY, 15000, 10});

bool modified = modify_book.modify_order(10001, 15200, 20);

std::cout << "Order Modified: "
          << (modified ? "YES" : "NO") << '\n';

std::cout << "New Best Bid: ₹"
          << static_cast<double>(modify_book.best_bid()) / 100.0
          << '\n';

std::cout << "New Remaining Quantity: "
          << modify_book.remaining_quantity(10001) << '\n';
std::cout << "\n--- Modified Order Matching Test ---\n";

OrderBook matching_book;

matching_book.add_order({10101, "AAPL", Side::SELL, 15000, 10});
matching_book.add_order({10102, "AAPL", Side::BUY, 14000, 5});

bool matching_modified = matching_book.modify_order(10102, 15000, 5);
std::cout << "Order Modified: "
          << (matching_modified ? "YES" : "NO") << '\n';

std::cout << "Executed Trades: "
          << matching_book.get_trades().size() << '\n';

for (const auto& trade : matching_book.get_trades())
{
    std::cout << "Buy Order: " << trade.buy_order_id
              << " | Sell Order: " << trade.sell_order_id
              << " | Quantity: " << trade.quantity << '\n';
}
std::cout << "\n--- Invalid Order Modification Test ---\n";

OrderBook invalid_modify_book;

invalid_modify_book.add_order({10201, "AAPL", Side::BUY, 15000, 10});

// Test 1: Non-existing order
bool missing_order = invalid_modify_book.modify_order(99999, 15500, 10);

// Test 2: Zero quantity
bool zero_quantity = invalid_modify_book.modify_order(10201, 15500, 0);

// Check original order
std::cout << "Non-existing Order Modified: "
          << (missing_order ? "YES" : "NO") << '\n';

std::cout << "Zero Quantity Modification: "
          << (zero_quantity ? "YES" : "NO") << '\n';

std::cout << "Original Order Exists: "
          << (invalid_modify_book.has_order(10201) ? "YES" : "NO") << '\n';

std::cout << "Original Remaining Quantity: "
          << invalid_modify_book.remaining_quantity(10201) << '\n';
    return 0;
}