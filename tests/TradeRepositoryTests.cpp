
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>

#include "TradeRepository.hpp"

TEST(TradeRepositoryTest, CreatesDatabase)
{
    TradeRepository repository(":memory:");

    EXPECT_TRUE(repository.get_all_trades().empty());
}

TEST(TradeRepositoryTest, SavesAndRetrievesTrade)
{
    TradeRepository repository(":memory:");

    std::vector<Trade> trades = {
        {101, 102, 15000, 10}
    };

    repository.save_trades(trades);

    auto result = repository.get_all_trades();

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].buy_order_id, 101);
    EXPECT_EQ(result[0].sell_order_id, 102);
    EXPECT_EQ(result[0].price_ticks, 15000);
    EXPECT_EQ(result[0].quantity, 10);
}

TEST(TradeRepositoryTest, SavesMultipleTrades)
{
    TradeRepository repository(":memory:");

    repository.save_trades({
        {101, 102, 15000, 10},
        {103, 104, 15500, 5},
        {105, 106, 16000, 8}
    });

    EXPECT_EQ(repository.get_all_trades().size(), 3);
}

TEST(TradeRepositoryTest, RollsBackBatchContainingInvalidTrade)
{
    TradeRepository repository(":memory:");
    EXPECT_THROW(repository.save_trades({
        {101, 102, 15000, 10, "AAPL"},
        {103, 104, 0, 5, "AAPL"}
    }), std::invalid_argument);
    EXPECT_TRUE(repository.get_all_trades().empty());
}

TEST(TradeRepositoryTest, LoadsSymbolAwareTradeRecordsIncrementally)
{
    TradeRepository repository(":memory:");
    repository.save_trades({
        {101, 102, 15000, 10, "AAPL"},
        {201, 202, 25000, 4, "TSLA"}
    });

    const auto all = repository.get_trade_records_after(0);
    ASSERT_EQ(all.size(), 2);
    EXPECT_EQ(all[0].symbol, "AAPL");
    EXPECT_EQ(all[0].trade.buy_order_id, 101);
    EXPECT_EQ(all[1].symbol, "TSLA");
    EXPECT_EQ(all[1].trade.sell_order_id, 202);

    const auto later = repository.get_trade_records_after(all[0].id);
    ASSERT_EQ(later.size(), 1);
    EXPECT_EQ(later[0].id, all[1].id);
    EXPECT_EQ(repository.get_all_trades()[0].symbol, "AAPL");
}

TEST(TradeRepositoryTest, PersistsExecutionTimestampsAndOwnedOrderSides)
{
    TradeRepository repository(":memory:");
    Trade trade{41, 42, 17800, 6, "AAPL"};
    trade.executed_at = std::chrono::system_clock::time_point{
        std::chrono::microseconds(1'700'000'123'456'789)};
    trade.buy_order_owned = true;
    repository.save_trades({trade});

    const auto records = repository.get_trade_records_after(0);
    ASSERT_EQ(records.size(), 1);
    ASSERT_TRUE(records[0].trade.executed_at.has_value());
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::microseconds>(
        records[0].trade.executed_at->time_since_epoch()).count(), 1'700'000'123'456'789);
    EXPECT_TRUE(records[0].trade.buy_order_owned);
    EXPECT_FALSE(records[0].trade.sell_order_owned);
}

TEST(TradeRepositoryTest, DoesNotInventTimestampWhenSavingTimestampFreeTrade)
{
    TradeRepository repository(":memory:");
    const Trade trade{51, 52, 18000, 1, "AAPL"};
    EXPECT_FALSE(trade.executed_at.has_value());
    repository.save_trades({trade});

    const auto records = repository.get_trade_records_after(0);
    ASSERT_EQ(records.size(), 1);
    EXPECT_FALSE(records[0].trade.executed_at.has_value());
}

TEST(TradeRepositoryTest, PersistsPortfolioOwnershipOnRestingOrders)
{
    TradeRepository repository(":memory:");
    Order owned_order{501, "AAPL", Side::BUY, 17000, 12};
    owned_order.portfolio_owned = true;
    repository.save_snapshot({owned_order});

    const auto restored = repository.load_latest_snapshot();
    ASSERT_EQ(restored.size(), 1);
    EXPECT_EQ(restored[0].order_id, 501);
    EXPECT_TRUE(restored[0].portfolio_owned);
}

TEST(TradeRepositoryTest, MigratesLegacyTradeAndSnapshotSchemas)
{
    const char* path = "legacy_trade_schema_test.db";
    std::remove(path);
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(path, &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db,
        "CREATE TABLE trades (id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "buy_order_id INTEGER NOT NULL,sell_order_id INTEGER NOT NULL,"
        "price_ticks INTEGER NOT NULL,quantity INTEGER NOT NULL);"
        "INSERT INTO trades(buy_order_id,sell_order_id,price_ticks,quantity)"
        "VALUES(7,8,12000,3);"
        "CREATE TABLE order_snapshots (id INTEGER PRIMARY KEY AUTOINCREMENT, completed INTEGER NOT NULL);"
        "CREATE TABLE snapshot_orders (snapshot_id INTEGER NOT NULL,sequence INTEGER NOT NULL,"
        "order_id INTEGER NOT NULL,symbol TEXT NOT NULL,side TEXT NOT NULL,price_ticks INTEGER NOT NULL,"
        "quantity INTEGER NOT NULL,order_type TEXT NOT NULL,status TEXT NOT NULL,"
        "PRIMARY KEY(snapshot_id,sequence),UNIQUE(snapshot_id,order_id));"
        "INSERT INTO order_snapshots(id,completed) VALUES(1,1);"
        "INSERT INTO snapshot_orders(snapshot_id,sequence,order_id,symbol,side,price_ticks,quantity,order_type,status)"
        "VALUES(1,0,17,'AAPL','BUY',15000,2,'LIMIT','OPEN');",
        nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);

    {
        TradeRepository repository(path);
        const auto records = repository.get_trade_records_after(0);
        ASSERT_EQ(records.size(), 1);
        EXPECT_EQ(records[0].trade.buy_order_id, 7);
        EXPECT_TRUE(records[0].symbol.empty());
        EXPECT_FALSE(records[0].trade.executed_at.has_value());
        const auto restored = repository.load_latest_snapshot();
        ASSERT_EQ(restored.size(), 1);
        EXPECT_FALSE(restored[0].portfolio_owned);
        repository.save_trades({{9, 10, 13000, 2, "NVDA"}});
        const auto updated = repository.get_trade_records_after(records[0].id);
        ASSERT_EQ(updated.size(), 1);
        EXPECT_EQ(updated[0].symbol, "NVDA");
    }
    std::remove(path);
}

TEST(TradeRepositoryTest, RollsBackPartialSchemaMigrationOnFailure)
{
    const char* path = "migration_rollback_test.db";
    std::remove(path);
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(path, &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db,
        "CREATE TABLE trades (id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "buy_order_id INTEGER NOT NULL,sell_order_id INTEGER NOT NULL,"
        "price_ticks INTEGER NOT NULL,quantity INTEGER NOT NULL);"
        "CREATE VIEW snapshot_orders AS SELECT 1 AS snapshot_id,0 AS sequence,"
        "1 AS order_id,'AAPL' AS symbol,'BUY' AS side,100 AS price_ticks,"
        "1 AS quantity,'LIMIT' AS order_type,'OPEN' AS status;",
        nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);

    EXPECT_THROW(TradeRepository repository(path), std::runtime_error);

    ASSERT_EQ(sqlite3_open(path, &db), SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db, "PRAGMA table_info(trades);", -1, &statement, nullptr), SQLITE_OK);
    bool symbol_column_exists = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (name && std::string(name) == "symbol") symbol_column_exists = true;
    }
    sqlite3_finalize(statement);
    sqlite3_close(db);
    EXPECT_FALSE(symbol_column_exists);
    std::remove(path);
}

TEST(TradeRepositoryTest, PreservesTradesAfterReopeningDatabase)
{
    const char* path = "trade_persistence_test.db";
    std::remove(path);

    {
        TradeRepository repository(path);
        Trade trade{201, 202, 15000, 10, "AAPL"};
        trade.executed_at = std::chrono::system_clock::time_point{
            std::chrono::microseconds(1'700'000'123'456'789)};
        trade.buy_order_owned = true;
        repository.save_trades({trade});
    }

    {
        TradeRepository repository(path);

        auto result = repository.get_all_trades();

        ASSERT_EQ(result.size(), 1);
        EXPECT_EQ(result[0].buy_order_id, 201);
        EXPECT_EQ(result[0].quantity, 10);
        ASSERT_TRUE(result[0].executed_at.has_value());
        EXPECT_TRUE(result[0].buy_order_owned);
        EXPECT_EQ(result[0].symbol, "AAPL");
    }

    std::remove(path);
}
