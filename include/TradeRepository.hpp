
#pragma once

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Trade.hpp"
#include "Order.hpp"

class TradeRepository
{
private:
    sqlite3* db = nullptr;

    static sqlite3_int64 to_sql_integer(std::uint64_t value)
    {
        if (value > static_cast<std::uint64_t>(
                std::numeric_limits<sqlite3_int64>::max()))
        {
            throw std::overflow_error("Value exceeds SQLite INTEGER range");
        }

        return static_cast<sqlite3_int64>(value);
    }

    void execute(const char* sql)
    {
        char* error = nullptr;

        if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK)
        {
            std::string message = error ? error : sqlite3_errmsg(db);
            sqlite3_free(error);
            throw std::runtime_error(message);
        }
    }

    static void check_bind(int rc, sqlite3* db)
    {
        if (rc != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
    }

    static const char* side_name(Side side) { return side == Side::BUY ? "BUY" : "SELL"; }
    static const char* type_name(OrderType type) { return type == OrderType::LIMIT ? "LIMIT" : "MARKET"; }
    static const char* status_name(OrderStatus status)
    {
        switch (status) {
        case OrderStatus::OPEN: return "OPEN";
        case OrderStatus::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
        default: return "INVALID";
        }
    }

    bool has_column(const std::string& table, const std::string& column) const
    {
        sqlite3_stmt* statement = nullptr;
        const std::string sql = "PRAGMA table_info(" + table + ");";
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
        bool found = false;
        int rc = SQLITE_OK;
        while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
            const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
            if (name && std::string(name) == column) found = true;
        }
        sqlite3_finalize(statement);
        if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
        return found;
    }

    static std::optional<std::chrono::system_clock::time_point> read_execution_time(
        sqlite3_stmt* statement, int column)
    {
        if (sqlite3_column_type(statement, column) == SQLITE_NULL)
            return std::nullopt;
        return std::chrono::system_clock::time_point{
            std::chrono::microseconds(sqlite3_column_int64(statement, column))};
    }

public:
    explicit TradeRepository(
        const std::string& database_path = "trading_engine.db",
        bool read_only = false)
    {
        const int open_flags = read_only
            ? SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX
            : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
        if (sqlite3_open_v2(database_path.c_str(), &db, open_flags, nullptr) != SQLITE_OK)
        {
            std::string message = db ? sqlite3_errmsg(db)
                                     : "Unable to open database";

            if (db)
                sqlite3_close(db);

            db = nullptr;
            throw std::runtime_error(message);
        }

        sqlite3_busy_timeout(db, 1000);
        if (read_only) return;

        try {
            execute("BEGIN IMMEDIATE;");
            execute(
                "CREATE TABLE IF NOT EXISTS trades ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "buy_order_id INTEGER NOT NULL,"
                "sell_order_id INTEGER NOT NULL,"
                "price_ticks INTEGER NOT NULL,"
                "quantity INTEGER NOT NULL,"
                "symbol TEXT NOT NULL DEFAULT '',"
                "execution_time_us INTEGER"
                ");"
            );
            if (!has_column("trades", "symbol"))
                execute("ALTER TABLE trades ADD COLUMN symbol TEXT NOT NULL DEFAULT ''; ");
            if (!has_column("trades", "execution_time_us"))
                execute("ALTER TABLE trades ADD COLUMN execution_time_us INTEGER;");
            if (!has_column("trades", "buy_order_owned"))
                execute("ALTER TABLE trades ADD COLUMN buy_order_owned INTEGER NOT NULL DEFAULT 0;");
            if (!has_column("trades", "sell_order_owned"))
                execute("ALTER TABLE trades ADD COLUMN sell_order_owned INTEGER NOT NULL DEFAULT 0;");
            execute("CREATE TABLE IF NOT EXISTS order_snapshots ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, completed INTEGER NOT NULL CHECK(completed IN (0,1)));"
                    "CREATE TABLE IF NOT EXISTS snapshot_orders ("
                    "snapshot_id INTEGER NOT NULL REFERENCES order_snapshots(id) ON DELETE CASCADE,"
                    "sequence INTEGER NOT NULL, order_id INTEGER NOT NULL, symbol TEXT NOT NULL,"
                    "side TEXT NOT NULL, price_ticks INTEGER NOT NULL, quantity INTEGER NOT NULL,"
                    "order_type TEXT NOT NULL, status TEXT NOT NULL, portfolio_owned INTEGER NOT NULL DEFAULT 0,"
                    "PRIMARY KEY(snapshot_id, sequence), UNIQUE(snapshot_id, order_id));");
            if (!has_column("snapshot_orders", "portfolio_owned"))
                execute("ALTER TABLE snapshot_orders ADD COLUMN portfolio_owned INTEGER NOT NULL DEFAULT 0;");
            execute("COMMIT;");
        } catch (...) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            sqlite3_close(db);
            db = nullptr;
            throw;
        }
    }

    ~TradeRepository()
    {
        if (db)
            sqlite3_close(db);
    }

    TradeRepository(const TradeRepository&) = delete;
    TradeRepository& operator=(const TradeRepository&) = delete;

    void save_snapshot(const std::vector<Order>& orders)
    {
        execute("BEGIN IMMEDIATE;");
        sqlite3_stmt *header = nullptr, *row = nullptr;
        try {
            if (sqlite3_prepare_v2(db, "INSERT INTO order_snapshots(completed) VALUES (0);", -1, &header, nullptr) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(db));
            if (sqlite3_step(header) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
            sqlite3_finalize(header); header = nullptr;
            const auto snapshot_id = sqlite3_last_insert_rowid(db);
            const char* sql = "INSERT INTO snapshot_orders(snapshot_id,sequence,order_id,symbol,side,price_ticks,quantity,order_type,status,portfolio_owned) VALUES(?,?,?,?,?,?,?,?,?,?);";
            if (sqlite3_prepare_v2(db, sql, -1, &row, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
            std::size_t sequence = 0;
            for (const auto& order : orders) {
                if (order.order_id == 0 || order.symbol.empty() || order.quantity == 0 || order.type != OrderType::LIMIT || order.price_ticks <= 0 ||
                    (order.status != OrderStatus::OPEN && order.status != OrderStatus::PARTIALLY_FILLED))
                    throw std::invalid_argument("Invalid active order in snapshot");
                check_bind(sqlite3_bind_int64(row, 1, snapshot_id), db);
                check_bind(sqlite3_bind_int64(row, 2, static_cast<sqlite3_int64>(sequence++)), db);
                check_bind(sqlite3_bind_int64(row, 3, to_sql_integer(order.order_id)), db);
                check_bind(sqlite3_bind_text(row, 4, order.symbol.c_str(), -1, SQLITE_TRANSIENT), db);
                check_bind(sqlite3_bind_text(row, 5, side_name(order.side), -1, SQLITE_STATIC), db);
                check_bind(sqlite3_bind_int64(row, 6, order.price_ticks), db);
                check_bind(sqlite3_bind_int64(row, 7, to_sql_integer(order.quantity)), db);
                check_bind(sqlite3_bind_text(row, 8, type_name(order.type), -1, SQLITE_STATIC), db);
                check_bind(sqlite3_bind_text(row, 9, status_name(order.status), -1, SQLITE_STATIC), db);
                check_bind(sqlite3_bind_int(row, 10, order.portfolio_owned ? 1 : 0), db);
                if (sqlite3_step(row) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
                sqlite3_reset(row); sqlite3_clear_bindings(row);
            }
            sqlite3_finalize(row); row = nullptr;
            sqlite3_stmt* finish = nullptr;
            if (sqlite3_prepare_v2(db, "UPDATE order_snapshots SET completed=1 WHERE id=?;", -1, &finish, nullptr) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(db));
            sqlite3_bind_int64(finish, 1, snapshot_id);
            if (sqlite3_step(finish) != SQLITE_DONE) { sqlite3_finalize(finish); throw std::runtime_error(sqlite3_errmsg(db)); }
            sqlite3_finalize(finish);
            execute("COMMIT;");
        } catch (...) {
            if (header) sqlite3_finalize(header);
            if (row) sqlite3_finalize(row);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            throw;
        }
    }

    std::vector<Order> load_latest_snapshot() const
    {
        sqlite3_stmt* snapshots = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT id FROM order_snapshots WHERE completed=1 ORDER BY id DESC;", -1, &snapshots, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
        try {
            while (sqlite3_step(snapshots) == SQLITE_ROW) {
                const auto id = sqlite3_column_int64(snapshots, 0);
                sqlite3_stmt* rows = nullptr;
                if (sqlite3_prepare_v2(db, "SELECT order_id,symbol,side,price_ticks,quantity,order_type,status,portfolio_owned FROM snapshot_orders WHERE snapshot_id=? ORDER BY sequence;", -1, &rows, nullptr) != SQLITE_OK)
                    throw std::runtime_error(sqlite3_errmsg(db));
                sqlite3_bind_int64(rows, 1, id);
                std::vector<Order> result;
                bool valid = true;
                const auto base = std::chrono::steady_clock::now();
                int rc;
                while ((rc = sqlite3_step(rows)) == SQLITE_ROW) {
                    const auto* symbol = reinterpret_cast<const char*>(sqlite3_column_text(rows, 1));
                    const auto* side = reinterpret_cast<const char*>(sqlite3_column_text(rows, 2));
                    const auto* type = reinterpret_cast<const char*>(sqlite3_column_text(rows, 5));
                    const auto* status = reinterpret_cast<const char*>(sqlite3_column_text(rows, 6));
                    if (!symbol || !side || !type || !status || (std::string(side) != "BUY" && std::string(side) != "SELL") ||
                        (std::string(type) != "LIMIT") || (std::string(status) != "OPEN" && std::string(status) != "PARTIALLY_FILLED") ||
                        sqlite3_column_int64(rows, 0) <= 0 || sqlite3_column_int64(rows, 3) <= 0 || sqlite3_column_int64(rows, 4) <= 0) { valid = false; break; }
                    Order order{static_cast<std::uint64_t>(sqlite3_column_int64(rows, 0)), symbol,
                        std::string(side) == "BUY" ? Side::BUY : Side::SELL, sqlite3_column_int64(rows, 3),
                        static_cast<std::uint64_t>(sqlite3_column_int64(rows, 4)), OrderType::LIMIT,
                        std::string(status) == "OPEN" ? OrderStatus::OPEN : OrderStatus::PARTIALLY_FILLED};
                    order.timestamp = base + std::chrono::nanoseconds(result.size());
                    order.portfolio_owned = sqlite3_column_int(rows, 7) != 0;
                    result.push_back(std::move(order));
                }
                sqlite3_finalize(rows);
                if (rc != SQLITE_DONE && valid) throw std::runtime_error(sqlite3_errmsg(db));
                if (valid) { sqlite3_finalize(snapshots); return result; }
            }
            sqlite3_finalize(snapshots);
        } catch (...) { sqlite3_finalize(snapshots); throw; }
        return {};
    }

    void save_trades(const std::vector<Trade>& trades)
    {
        execute("BEGIN TRANSACTION;");

        sqlite3_stmt* statement = nullptr;

        try
        {
            const char* sql =
                "INSERT INTO trades "
                "(buy_order_id, sell_order_id, price_ticks, quantity, symbol, execution_time_us, buy_order_owned, sell_order_owned) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

            if (sqlite3_prepare_v2(
                    db, sql, -1, &statement, nullptr) != SQLITE_OK)
            {
                throw std::runtime_error(sqlite3_errmsg(db));
            }

            for (const auto& trade : trades)
            {
                if (trade.buy_order_id == 0 || trade.sell_order_id == 0 ||
                    trade.price_ticks <= 0 || trade.quantity == 0)
                    throw std::invalid_argument("Invalid executed trade");

                check_bind(sqlite3_bind_int64(
                    statement, 1, to_sql_integer(trade.buy_order_id)), db);
                check_bind(sqlite3_bind_int64(
                    statement, 2, to_sql_integer(trade.sell_order_id)), db);
                check_bind(sqlite3_bind_int64(
                    statement, 3, trade.price_ticks), db);
                check_bind(sqlite3_bind_int64(
                    statement, 4, to_sql_integer(trade.quantity)), db);
                check_bind(sqlite3_bind_text(
                    statement, 5, trade.symbol.c_str(), -1, SQLITE_TRANSIENT), db);

                if (trade.executed_at) {
                    const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                        trade.executed_at->time_since_epoch()).count();
                    check_bind(sqlite3_bind_int64(statement, 6, timestamp), db);
                } else {
                    check_bind(sqlite3_bind_null(statement, 6), db);
                }
                check_bind(sqlite3_bind_int(statement, 7, trade.buy_order_owned ? 1 : 0), db);
                check_bind(sqlite3_bind_int(statement, 8, trade.sell_order_owned ? 1 : 0), db);

                if (sqlite3_step(statement) != SQLITE_DONE)
                    throw std::runtime_error(sqlite3_errmsg(db));

                check_bind(sqlite3_reset(statement), db);
                check_bind(sqlite3_clear_bindings(statement), db);
            }

            sqlite3_finalize(statement);
            statement = nullptr;

            execute("COMMIT;");
        }
        catch (...)
        {
            if (statement)
                sqlite3_finalize(statement);

            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            throw;
        }
    }

    std::vector<Trade> get_all_trades() const
    {
        std::vector<Trade> trades;

        const char* sql =
            "SELECT buy_order_id, sell_order_id, price_ticks, quantity, symbol, execution_time_us, buy_order_owned, sell_order_owned "
            "FROM trades ORDER BY id;";

        sqlite3_stmt* statement = nullptr;

        if (sqlite3_prepare_v2(
                db, sql, -1, &statement, nullptr) != SQLITE_OK)
        {
            throw std::runtime_error(sqlite3_errmsg(db));
        }

        try
        {
            int rc = SQLITE_OK;
            while ((rc = sqlite3_step(statement)) == SQLITE_ROW)
            {
                Trade trade{
                    static_cast<std::uint64_t>(
                        sqlite3_column_int64(statement, 0)),
                    static_cast<std::uint64_t>(
                        sqlite3_column_int64(statement, 1)),
                    sqlite3_column_int64(statement, 2),
                    static_cast<std::uint64_t>(
                        sqlite3_column_int64(statement, 3)),
                    reinterpret_cast<const char*>(sqlite3_column_text(statement, 4))
                };
                trade.executed_at = read_execution_time(statement, 5);
                trade.buy_order_owned = sqlite3_column_int(statement, 6) != 0;
                trade.sell_order_owned = sqlite3_column_int(statement, 7) != 0;
                trades.push_back(std::move(trade));
            }

            if (rc != SQLITE_DONE)
                throw std::runtime_error(sqlite3_errmsg(db));

            sqlite3_finalize(statement);
        }
        catch (...)
        {
            sqlite3_finalize(statement);
            throw;
        }

        return trades;
    }

    std::vector<TradeRecord> get_trade_records_after(
        std::int64_t last_id, std::size_t limit = 500) const
    {
        sqlite3_stmt* statement = nullptr;
        const char* sql =
            "SELECT id, symbol, buy_order_id, sell_order_id, price_ticks, quantity, execution_time_us, buy_order_owned, sell_order_owned "
            "FROM trades WHERE id > ? ORDER BY id LIMIT ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
        try {
            sqlite3_bind_int64(statement, 1, last_id);
            const auto bounded_limit = std::min<std::size_t>(
                limit, static_cast<std::size_t>(std::numeric_limits<sqlite3_int64>::max()));
            sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(bounded_limit));
            std::vector<TradeRecord> records;
            int rc;
            while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
                const auto* symbol = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
                Trade trade{
                    static_cast<std::uint64_t>(sqlite3_column_int64(statement, 2)),
                    static_cast<std::uint64_t>(sqlite3_column_int64(statement, 3)),
                    sqlite3_column_int64(statement, 4),
                    static_cast<std::uint64_t>(sqlite3_column_int64(statement, 5)),
                    symbol ? symbol : ""
                };
                trade.executed_at = read_execution_time(statement, 6);
                trade.buy_order_owned = sqlite3_column_int(statement, 7) != 0;
                trade.sell_order_owned = sqlite3_column_int(statement, 8) != 0;
                records.push_back({sqlite3_column_int64(statement, 0), trade.symbol, std::move(trade)});
            }
            if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
            sqlite3_finalize(statement);
            return records;
        } catch (...) {
            sqlite3_finalize(statement);
            throw;
        }
    }
};
