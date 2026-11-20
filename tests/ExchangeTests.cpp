
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include "Exchange.hpp"
#include "TradeRepository.hpp"
#include "TradingAnalytics.hpp"
#include <cstdio>

TEST(ExchangeSnapshotTest, RestoresOrdersAcrossRestartWithoutReplayingTrades)
{
    TradeRepository repository(":memory:");
    {
        Exchange exchange(repository);
        exchange.add_order({1, "AAPL", Side::SELL, 15000, 10});
        exchange.add_order({2, "TSLA", Side::BUY, 25000, 7});
        exchange.add_order({3, "AAPL", Side::BUY, 15000, 4});
        ASSERT_EQ(repository.get_all_trades().size(), 1);
        exchange.save_snapshot();
    }
    Exchange recovered(repository);
    EXPECT_EQ(recovered.get_order_count("AAPL"), 1);
    EXPECT_EQ(recovered.remaining_quantity("AAPL", 1), 6);
    EXPECT_EQ(recovered.get_order_count("TSLA"), 1);
    EXPECT_EQ(recovered.best_bid("TSLA"), 25000);
    EXPECT_EQ(repository.get_all_trades().size(), 1);
}

TEST(ExchangeSnapshotTest, PreservesFifoPriorityAndMatchesNewOrders)
{
    TradeRepository repository(":memory:");
    {
        Exchange exchange(repository);
        exchange.add_order({10, "AAPL", Side::SELL, 15000, 3});
        exchange.add_order({11, "AAPL", Side::SELL, 15000, 5});
        exchange.save_snapshot();
    }
    Exchange recovered(repository);
    recovered.add_order({12, "AAPL", Side::BUY, 15000, 4});
    const auto& trades = recovered.get_trades("AAPL");
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].sell_order_id, 10);
    EXPECT_EQ(trades[0].quantity, 3);
    EXPECT_EQ(trades[1].sell_order_id, 11);
    EXPECT_EQ(trades[1].quantity, 1);
    EXPECT_EQ(recovered.remaining_quantity("AAPL", 11), 4);
}

TEST(ExchangeSnapshotTest, SavesAndRestoresEmptyBook)
{
    TradeRepository repository(":memory:");
    { Exchange exchange(repository); exchange.save_snapshot(); }
    Exchange recovered(repository);
    EXPECT_EQ(recovered.get_order_count("AAPL"), 0);
}

TEST(ExchangeSnapshotTest, LoadsLatestValidSnapshotWhenNewestIsCorrupt)
{
    const char* path = "snapshot_corruption_test.db";
    std::remove(path);
    {
        TradeRepository repository(path);
        repository.save_snapshot({Order{21, "MSFT", Side::BUY, 32000, 6}});
        repository.save_snapshot({Order{22, "MSFT", Side::BUY, 32100, 4}});
    }
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(path, &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "UPDATE snapshot_orders SET status='BROKEN' WHERE order_id=22;", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);
    {
        TradeRepository repository(path);
        Exchange recovered(repository);
        EXPECT_EQ(recovered.remaining_quantity("MSFT", 21), 6);
        EXPECT_EQ(recovered.remaining_quantity("MSFT", 22), 0);
    }
    std::remove(path);
}

TEST(ExchangeRecoveryIntegrationTest, RecoversFileBackedExchangeAcrossFullRestart)
{
    const auto database_path = std::filesystem::temp_directory_path() /
        ("trading_engine_recovery_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
    const std::string path = database_path.string();
    const auto remove_database = [&]() {
        std::error_code error;
        std::filesystem::remove(database_path, error);
        std::filesystem::remove(path + "-wal", error);
        std::filesystem::remove(path + "-shm", error);
    };
    remove_database();

    {
        TradeRepository repository(path);
        {
            Exchange exchange(repository);
            ASSERT_TRUE(exchange.add_order({101, "AAPL", Side::SELL, 15000, 10}));
            ASSERT_TRUE(exchange.add_order({102, "AAPL", Side::SELL, 15000, 5}));
            ASSERT_TRUE(exchange.add_order({103, "AAPL", Side::BUY, 15000, 4}));
            ASSERT_TRUE(exchange.add_order({201, "MSFT", Side::BUY, 32000, 7}));
            exchange.save_snapshot();

            OrderStatus status{};
            ASSERT_TRUE(exchange.get_order_status("AAPL", 101, status));
            EXPECT_EQ(status, OrderStatus::PARTIALLY_FILLED);
            EXPECT_EQ(exchange.remaining_quantity("AAPL", 101), 6);
            ASSERT_EQ(repository.get_all_trades().size(), 1);
        } // Exchange shutdown
    } // Close SQLite connection to exercise reopening the database

    {
        TradeRepository repository(path);
        Exchange recovered(repository);
        EXPECT_EQ(recovered.get_order_count("AAPL"), 2);
        EXPECT_EQ(recovered.get_order_count("MSFT"), 1);
        EXPECT_TRUE(recovered.has_order("AAPL", 101));
        EXPECT_TRUE(recovered.has_order("AAPL", 102));
        EXPECT_TRUE(recovered.has_order("MSFT", 201));
        EXPECT_EQ(recovered.remaining_quantity("AAPL", 101), 6);
        EXPECT_EQ(recovered.remaining_quantity("AAPL", 102), 5);
        EXPECT_EQ(recovered.remaining_quantity("MSFT", 201), 7);

        OrderStatus status{};
        ASSERT_TRUE(recovered.get_order_status("AAPL", 101, status));
        EXPECT_EQ(status, OrderStatus::PARTIALLY_FILLED);
        ASSERT_TRUE(recovered.get_order_status("AAPL", 102, status));
        EXPECT_EQ(status, OrderStatus::OPEN);

        // Recovery did not rematch the crossed remainder or duplicate its persisted trade.
        EXPECT_EQ(repository.get_all_trades().size(), 1);
        ASSERT_TRUE(recovered.add_order({104, "AAPL", Side::BUY, 15000, 8}));

        const auto trades = repository.get_all_trades();
        ASSERT_EQ(trades.size(), 3);
        EXPECT_EQ(trades[1].buy_order_id, 104);
        EXPECT_EQ(trades[1].sell_order_id, 101);
        EXPECT_EQ(trades[1].quantity, 6);
        EXPECT_EQ(trades[2].buy_order_id, 104);
        EXPECT_EQ(trades[2].sell_order_id, 102);
        EXPECT_EQ(trades[2].quantity, 2);
        EXPECT_EQ(recovered.remaining_quantity("AAPL", 102), 3);
    }

    remove_database();
}

TEST(ExchangeTest, RoutesOrdersToCorrectSymbol)
{
    Exchange exchange;

    EXPECT_TRUE(exchange.add_order(
        {1, "AAPL", Side::BUY, 15000, 10}
    ));

    EXPECT_TRUE(exchange.add_order(
        {2, "TSLA", Side::BUY, 25000, 5}
    ));

    EXPECT_TRUE(exchange.has_symbol("AAPL"));
    EXPECT_TRUE(exchange.has_symbol("TSLA"));

    EXPECT_EQ(exchange.best_bid("AAPL"), 15000);
    EXPECT_EQ(exchange.best_bid("TSLA"), 25000);
}

TEST(ExchangeTest, KeepsSymbolsIsolated)
{
    Exchange exchange;

    exchange.add_order({1, "AAPL", Side::BUY, 15000, 10});
    exchange.add_order({2, "TSLA", Side::SELL, 25000, 5});

    EXPECT_EQ(exchange.get_order_count("AAPL"), 1);
    EXPECT_EQ(exchange.get_order_count("TSLA"), 1);

    EXPECT_EQ(exchange.best_ask("AAPL"), 0);
    EXPECT_EQ(exchange.best_bid("TSLA"), 0);
}

TEST(ExchangeTest, CancelsOrderBySymbol)
{
    Exchange exchange;

    exchange.add_order({1, "AAPL", Side::BUY, 15000, 10});

    EXPECT_TRUE(exchange.cancel_order("AAPL", 1));
    EXPECT_FALSE(exchange.has_order("AAPL", 1));
    EXPECT_EQ(exchange.get_order_count("AAPL"), 0);
}

TEST(ExchangeTest, ReturnsCorrectOrderStatus)
{
    Exchange exchange;

    exchange.add_order({1, "AAPL", Side::BUY, 15000, 10});

    OrderStatus status;

    EXPECT_TRUE(exchange.get_order_status("AAPL", 1, status));
    EXPECT_EQ(status, OrderStatus::OPEN);

    EXPECT_FALSE(exchange.get_order_status("TSLA", 1, status));
}

TEST(ExchangeTest, MaintainsSeparateTradeHistory)
{
    Exchange exchange;

    exchange.add_order({1, "AAPL", Side::SELL, 15000, 5});
    exchange.add_order({2, "AAPL", Side::BUY, 15000, 5});

    exchange.add_order({3, "TSLA", Side::SELL, 25000, 3});
    exchange.add_order({4, "TSLA", Side::BUY, 25000, 3});

    EXPECT_EQ(exchange.get_trades("AAPL").size(), 1);
    EXPECT_EQ(exchange.get_trades("TSLA").size(), 1);

    EXPECT_EQ(exchange.get_trades("AAPL")[0].quantity, 5);
    EXPECT_EQ(exchange.get_trades("TSLA")[0].quantity, 3);
}
TEST(ExchangeTest, RejectsDuplicateOrderId)
{
    Exchange exchange;

    EXPECT_TRUE(exchange.add_order(
        {1, "AAPL", Side::BUY, 15000, 10}
    ));

    EXPECT_FALSE(exchange.add_order(
        {1, "AAPL", Side::SELL, 15100, 5}
    ));
}

TEST(ExchangeTest, RejectsInvalidOrder)
{
    Exchange exchange;

    EXPECT_FALSE(exchange.add_order(
        {0, "AAPL", Side::BUY, 15000, 10}
    ));

    EXPECT_FALSE(exchange.add_order(
        {1, "", Side::BUY, 15000, 10}
    ));

    EXPECT_FALSE(exchange.add_order(
        {2, "AAPL", Side::BUY, 15000, 0}
    ));

    EXPECT_FALSE(exchange.add_order(
        {3, "AAPL", Side::BUY, 0, 10}
    ));
}

TEST(ExchangeTest, ReturnsZeroForUnknownSymbol)
{
    Exchange exchange;

    EXPECT_EQ(exchange.best_bid("UNKNOWN"), 0);
    EXPECT_EQ(exchange.best_ask("UNKNOWN"), 0);
    EXPECT_EQ(exchange.get_order_count("UNKNOWN"), 0);
}

TEST(ExchangeTest, TracksRemainingQuantity)
{
    Exchange exchange;

    exchange.add_order({1, "AAPL", Side::SELL, 15000, 10});
    exchange.add_order({2, "AAPL", Side::BUY, 15000, 4});

    EXPECT_EQ(exchange.remaining_quantity("AAPL", 1), 6);
    EXPECT_EQ(exchange.remaining_quantity("AAPL", 2), 0);
}

TEST(ExchangeTest, CancellingOneSymbolDoesNotAffectAnother)
{
    Exchange exchange;

    exchange.add_order({1, "AAPL", Side::BUY, 15000, 10});
    exchange.add_order({2, "TSLA", Side::BUY, 25000, 5});

    EXPECT_TRUE(exchange.cancel_order("AAPL", 1));

    EXPECT_FALSE(exchange.has_order("AAPL", 1));
    EXPECT_TRUE(exchange.has_order("TSLA", 2));

    EXPECT_EQ(exchange.best_bid("TSLA"), 25000);
}
TEST(ExchangeTest, AutomaticallyPersistsExecutedTrades)
{
    TradeRepository repository(":memory:");
    Exchange exchange(repository);

    EXPECT_TRUE(exchange.add_order(
        {1, "AAPL", Side::SELL, 15000, 10}
    ));

    EXPECT_TRUE(exchange.add_order(
        {2, "AAPL", Side::BUY, 15000, 10}
    ));

    auto persisted_trades = repository.get_all_trades();

    ASSERT_EQ(persisted_trades.size(), 1);
    EXPECT_EQ(persisted_trades[0].buy_order_id, 2);
    EXPECT_EQ(persisted_trades[0].sell_order_id, 1);
    EXPECT_EQ(persisted_trades[0].price_ticks, 15000);
    EXPECT_EQ(persisted_trades[0].quantity, 10);
}

TEST(ExchangeTest, PersistsTradesAcrossMultipleSymbols)
{
    TradeRepository repository(":memory:");
    Exchange exchange(repository);

    exchange.add_order(
        {1, "AAPL", Side::SELL, 15000, 5}
    );
    exchange.add_order(
        {2, "AAPL", Side::BUY, 15000, 5}
    );

    exchange.add_order(
        {3, "TSLA", Side::SELL, 25000, 3}
    );
    exchange.add_order(
        {4, "TSLA", Side::BUY, 25000, 3}
    );

    auto persisted_trades = repository.get_all_trades();

    ASSERT_EQ(persisted_trades.size(), 2);

    EXPECT_EQ(persisted_trades[0].quantity, 5);
    EXPECT_EQ(persisted_trades[0].symbol, "AAPL");
    EXPECT_EQ(persisted_trades[1].quantity, 3);
    EXPECT_EQ(persisted_trades[1].symbol, "TSLA");
}

TEST(ExchangeTest, PersistsOwnedBuyAndSellExecutionLegs)
{
    TradeRepository repository(":memory:");
    Exchange exchange(repository);
    Order owned_sell{301, "AAPL", Side::SELL, 15000, 5};
    owned_sell.portfolio_owned = true;
    ASSERT_TRUE(exchange.add_order(owned_sell));
    ASSERT_TRUE(exchange.add_order({302, "AAPL", Side::BUY, 15000, 5}));

    ASSERT_TRUE(exchange.add_order({303, "MSFT", Side::SELL, 25000, 3}));
    Order owned_buy{304, "MSFT", Side::BUY, 25000, 3};
    owned_buy.portfolio_owned = true;
    ASSERT_TRUE(exchange.add_order(owned_buy));

    const auto records = repository.get_trade_records_after(0);
    ASSERT_EQ(records.size(), 2);
    EXPECT_TRUE(records[0].trade.sell_order_owned);
    EXPECT_FALSE(records[0].trade.buy_order_owned);
    EXPECT_TRUE(records[1].trade.buy_order_owned);
    EXPECT_FALSE(records[1].trade.sell_order_owned);
    EXPECT_TRUE(records[0].trade.executed_at.has_value());
    EXPECT_TRUE(records[1].trade.executed_at.has_value());
}

TEST(ExchangeSnapshotTest, RestoredGuiOrderRetainsPortfolioOwnershipWhenFilled)
{
    const char* path = "portfolio_ownership_recovery_test.db";
    std::remove(path);
    {
        TradeRepository repository(path);
        {
            Exchange exchange(repository);
            Order gui_sell{401, "AAPL", Side::SELL, 15100, 5};
            gui_sell.portfolio_owned = true;
            ASSERT_TRUE(exchange.add_order(gui_sell));
            exchange.save_snapshot();
        }
    }

    {
        TradeRepository repository(path);
        Exchange recovered(repository);
        ASSERT_TRUE(recovered.has_order("AAPL", 401));
        ASSERT_TRUE(recovered.add_order({402, "AAPL", Side::BUY, 15200, 5}));
        const auto trades = repository.get_trade_records_after(0);
        ASSERT_EQ(trades.size(), 1);
        EXPECT_TRUE(trades[0].trade.sell_order_owned);
        EXPECT_TRUE(trades[0].trade.executed_at.has_value());
        const auto portfolio = calculate_portfolio(trades);
        ASSERT_EQ(portfolio.size(), 1);
        EXPECT_EQ(portfolio[0].net_quantity, -5);
    }
    std::remove(path);
}
