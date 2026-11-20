#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "TradingAnalytics.hpp"

namespace {
using Clock = std::chrono::system_clock;
using namespace std::chrono_literals;

TradeRecord make_record(std::int64_t id, std::string symbol, std::int64_t price,
                        std::uint64_t quantity, std::chrono::seconds offset,
                        bool buy_owned = false, bool sell_owned = false)
{
    Trade trade{static_cast<std::uint64_t>(id * 10), static_cast<std::uint64_t>(id * 10 + 1),
                price, quantity, symbol};
    trade.executed_at = Clock::time_point{} + offset;
    trade.buy_order_owned = buy_owned;
    trade.sell_order_owned = sell_owned;
    return {id, std::move(symbol), std::move(trade)};
}
}

TEST(TradingAnalyticsTest, CalculatesOhlcAndVolumeFromExecutionPrices)
{
    const std::vector<TradeRecord> records{
        make_record(1, "AAPL", 10000, 3, 5s),
        make_record(2, "AAPL", 10300, 7, 22s),
        make_record(3, "AAPL", 9900, 2, 58s),
        make_record(4, "AAPL", 10100, 4, 61s)
    };

    const auto candles = build_candlesticks(records, "AAPL", 1min);
    ASSERT_EQ(candles.size(), 2);
    EXPECT_EQ(candles[0].open_ticks, 10000);
    EXPECT_EQ(candles[0].high_ticks, 10300);
    EXPECT_EQ(candles[0].low_ticks, 9900);
    EXPECT_EQ(candles[0].close_ticks, 9900);
    EXPECT_EQ(candles[0].volume, 12);
    EXPECT_EQ(candles[0].trade_count, 3);
    EXPECT_EQ(candles[1].open_ticks, 10100);
    EXPECT_EQ(candles[1].close_ticks, 10100);
    EXPECT_EQ(candles[1].volume, 4);
}

TEST(TradingAnalyticsTest, GroupsTradesBySelectableIntervalsAndSymbol)
{
    const std::vector<TradeRecord> records{
        make_record(1, "AAPL", 10000, 1, 10s),
        make_record(2, "AAPL", 10100, 1, 70s),
        make_record(3, "AAPL", 10200, 1, 130s),
        make_record(4, "MSFT", 22000, 1, 20s),
        make_record(5, "AAPL", 10300, 1, 610s),
        make_record(6, "AAPL", 10400, 1, 1300s),
        make_record(7, "AAPL", 10500, 1, 3700s)
    };

    EXPECT_EQ(build_candlesticks(records, "AAPL", 1min).size(), 6);
    EXPECT_EQ(build_candlesticks(records, "AAPL", 5min).size(), 4);
    EXPECT_EQ(build_candlesticks(records, "AAPL", 15min).size(), 3);
    EXPECT_EQ(build_candlesticks(records, "AAPL", 1h).size(), 2);
    EXPECT_EQ(build_candlesticks(records, "MSFT", 1min).size(), 1);
    EXPECT_TRUE(build_candlesticks(records, "UNKNOWN", 1min).empty());
}

TEST(TradingAnalyticsTest, IgnoresTradesWithoutExecutionTimestamps)
{
    auto record = make_record(1, "AAPL", 10000, 2, 1s);
    record.trade.executed_at = std::nullopt;
    EXPECT_TRUE(build_candlesticks({record}, "AAPL", 1min).empty());
}

TEST(TradingAnalyticsTest, ReturnsNoCandlesForEmptyHistory)
{
    EXPECT_TRUE(build_candlesticks({}, "AAPL", 1min).empty());
    EXPECT_THROW(build_candlesticks({}, "AAPL", 0ms), std::invalid_argument);
}

TEST(TradingAnalyticsTest, CalculatesOwnedBuyAndSellPositionsWithPartialFills)
{
    const std::vector<TradeRecord> records{
        make_record(1, "AAPL", 10000, 3, 1s, true, false),
        make_record(2, "AAPL", 10000, 2, 2s, true, false),
        make_record(3, "AAPL", 12000, 5, 3s, true, false),
        make_record(4, "AAPL", 13000, 6, 4s, false, true),
        make_record(5, "MSFT", 20000, 4, 5s, true, false)
    };

    const auto portfolio = calculate_portfolio(records);
    ASSERT_EQ(portfolio.size(), 2);
    ASSERT_EQ(portfolio[0].symbol, "AAPL");
    EXPECT_EQ(portfolio[0].net_quantity, 4);
    EXPECT_DOUBLE_EQ(portfolio[0].average_entry_ticks, 11000.0);
    EXPECT_EQ(portfolio[0].last_trade_price_ticks, 13000);
    EXPECT_DOUBLE_EQ(portfolio[0].position_value, 520.0);
    EXPECT_DOUBLE_EQ(portfolio[0].realized_pnl, 120.0);
    EXPECT_DOUBLE_EQ(portfolio[0].unrealized_pnl, 80.0);
    EXPECT_EQ(portfolio[1].symbol, "MSFT");
    EXPECT_EQ(portfolio[1].net_quantity, 4);
}

TEST(TradingAnalyticsTest, CalculatesShortPositionsAndZeroPositionRealizedPnl)
{
    const std::vector<TradeRecord> records{
        make_record(1, "SHORT", 5000, 4, 1s, false, true),
        make_record(2, "SHORT", 4000, 1, 2s, true, false),
        make_record(3, "FLAT", 5000, 5, 3s, true, false),
        make_record(4, "FLAT", 5500, 5, 4s, false, true)
    };

    const auto portfolio = calculate_portfolio(records);
    ASSERT_EQ(portfolio.size(), 2);
    EXPECT_EQ(portfolio[0].symbol, "FLAT");
    EXPECT_EQ(portfolio[0].net_quantity, 0);
    EXPECT_DOUBLE_EQ(portfolio[0].average_entry_ticks, 0.0);
    EXPECT_DOUBLE_EQ(portfolio[0].position_value, 0.0);
    EXPECT_DOUBLE_EQ(portfolio[0].realized_pnl, 25.0);
    EXPECT_DOUBLE_EQ(portfolio[0].unrealized_pnl, 0.0);
    EXPECT_EQ(portfolio[1].symbol, "SHORT");
    EXPECT_EQ(portfolio[1].net_quantity, -3);
    EXPECT_DOUBLE_EQ(portfolio[1].realized_pnl, 10.0);
    EXPECT_DOUBLE_EQ(portfolio[1].unrealized_pnl, 30.0);
}

TEST(TradingAnalyticsTest, EmptyHistoryHasNoPortfolioPositions)
{
    EXPECT_TRUE(calculate_portfolio({}).empty());
}
