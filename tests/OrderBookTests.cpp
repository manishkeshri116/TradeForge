
#include <gtest/gtest.h>
#include "OrderBook.hpp"

TEST(OrderBookTest, AcceptsValidOrder)
{
    OrderBook book;

    Order order{1, "AAPL", Side::BUY, 15000, 10};

    EXPECT_TRUE(book.add_order(order));
}

TEST(OrderBookTest, RejectsInvalidOrder)
{
    OrderBook book;

    Order order{0, "AAPL", Side::BUY, 15000, 10};

    EXPECT_FALSE(book.add_order(order));
}

TEST(OrderBookTest, MatchesBuyAndSellOrders)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::SELL, 15000, 10});
    book.add_order({2, "AAPL", Side::BUY, 15000, 10});

    EXPECT_EQ(book.get_trades().size(), 1);
    EXPECT_EQ(book.get_trades()[0].quantity, 10);
}

TEST(OrderBookTest, CancelsExistingOrder)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::BUY, 15000, 10});

    EXPECT_TRUE(book.cancel_order(1));
    EXPECT_FALSE(book.has_order(1));
}

TEST(OrderBookTest, RejectsInvalidModification)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::BUY, 15000, 10});

    EXPECT_FALSE(book.modify_order(1, 15500, 0));
    EXPECT_TRUE(book.has_order(1));
    EXPECT_EQ(book.remaining_quantity(1), 10);
}
TEST(OrderBookTest, SupportsPartialFill)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::SELL, 15000, 10});
    book.add_order({2, "AAPL", Side::BUY, 15000, 5});

    EXPECT_EQ(book.get_trades().size(), 1);
    EXPECT_EQ(book.get_trades()[0].quantity, 5);
    EXPECT_EQ(book.remaining_quantity(1), 5);
}

TEST(OrderBookTest, FollowsFIFOPriority)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::SELL, 15000, 5});
    book.add_order({2, "AAPL", Side::SELL, 15000, 10});
    book.add_order({3, "AAPL", Side::BUY, 15000, 8});

    ASSERT_EQ(book.get_trades().size(), 2);

    EXPECT_EQ(book.get_trades()[0].sell_order_id, 1);
    EXPECT_EQ(book.get_trades()[0].quantity, 5);

    EXPECT_EQ(book.get_trades()[1].sell_order_id, 2);
    EXPECT_EQ(book.get_trades()[1].quantity, 3);
}

TEST(OrderBookTest, ExecutesMarketBuyOrder)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::SELL, 15000, 10});

    Order market_buy{2, "AAPL", Side::BUY, 0, 5};
    market_buy.type = OrderType::MARKET;

    EXPECT_TRUE(book.add_order(market_buy));

    ASSERT_EQ(book.get_trades().size(), 1);
    EXPECT_EQ(book.get_trades()[0].quantity, 5);
    EXPECT_EQ(book.remaining_quantity(1), 5);
}
TEST(OrderBookTest, UpdatesOrderStatusAfterPartialFill)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::SELL, 15000, 10});

    Order market_buy{2, "AAPL", Side::BUY, 0, 5};
    market_buy.type = OrderType::MARKET;

    book.add_order(market_buy);

    OrderStatus status;

    EXPECT_TRUE(book.get_order_status(1, status));
    EXPECT_EQ(status, OrderStatus::PARTIALLY_FILLED);


}
TEST(OrderBookTest, UpdatesOrderStatusAfterFullFill)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::SELL, 15000, 10});
    book.add_order({2, "AAPL", Side::BUY, 15000, 10});

    OrderStatus status;

    EXPECT_TRUE(book.get_order_status(1, status));
    EXPECT_EQ(status, OrderStatus::FILLED);

    EXPECT_TRUE(book.get_order_status(2, status));
    EXPECT_EQ(status, OrderStatus::FILLED);
}
TEST(OrderBookTest, UpdatesOrderStatusAfterCancellation)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::BUY, 15000, 10});

    EXPECT_TRUE(book.cancel_order(1));

    OrderStatus status;

    EXPECT_TRUE(book.get_order_status(1, status));
    EXPECT_EQ(status, OrderStatus::CANCELLED);
}
TEST(OrderBookTest, RejectsDuplicateOrderId)
{
    OrderBook book;

    EXPECT_TRUE(
        book.add_order({1, "AAPL", Side::BUY, 15000, 10})
    );

    EXPECT_FALSE(
        book.add_order({1, "AAPL", Side::SELL, 15100, 5})
    );

    EXPECT_EQ(book.total_orders(), 1);
}

TEST(OrderBookTest, RejectsReusedCancelledOrderId)
{
    OrderBook book;

    EXPECT_TRUE(
        book.add_order({1, "AAPL", Side::BUY, 15000, 10})
    );

    EXPECT_TRUE(book.cancel_order(1));

    EXPECT_FALSE(
        book.add_order({1, "AAPL", Side::SELL, 15100, 5})
    );

    EXPECT_EQ(book.total_orders(), 0);
}
TEST(OrderBookTest, ExecutesMarketSellOrder)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::BUY, 15000, 10});

    Order market_sell{2, "AAPL", Side::SELL, 0, 5};
    market_sell.type = OrderType::MARKET;

    EXPECT_TRUE(book.add_order(market_sell));

    ASSERT_EQ(book.get_trades().size(), 1);
    EXPECT_EQ(book.get_trades()[0].quantity, 5);
    EXPECT_EQ(book.remaining_quantity(1), 5);
}
TEST(OrderBookTest, NewOrderHasOpenStatus)
{
    OrderBook book;

    EXPECT_TRUE(
        book.add_order({1, "AAPL", Side::BUY, 15000, 10})
    );

    OrderStatus status;

    EXPECT_TRUE(book.get_order_status(1, status));
    EXPECT_EQ(status, OrderStatus::OPEN);
}
TEST(OrderBookTest, MarketOrderWithNoLiquidity)
{
    OrderBook book;

    Order market_buy{1, "AAPL", Side::BUY, 0, 10};
    market_buy.type = OrderType::MARKET;

    EXPECT_TRUE(book.add_order(market_buy));

    EXPECT_EQ(book.get_trades().size(), 0);
    EXPECT_EQ(book.total_orders(), 0);
}
TEST(OrderBookTest, MarketOrderWithInsufficientLiquidity)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::SELL, 15000, 5});

    Order market_buy{2, "AAPL", Side::BUY, 0, 10};
    market_buy.type = OrderType::MARKET;

    EXPECT_TRUE(book.add_order(market_buy));

    ASSERT_EQ(book.get_trades().size(), 1);
    EXPECT_EQ(book.get_trades()[0].quantity, 5);
    EXPECT_EQ(book.remaining_quantity(1), 0);
    EXPECT_EQ(book.total_orders(), 0);
}
TEST(OrderBookTest, RejectsZeroQuantity)
{
    OrderBook book;

    EXPECT_FALSE(
        book.add_order({1, "AAPL", Side::BUY, 15000, 0})
    );

    EXPECT_EQ(book.total_orders(), 0);
}

TEST(OrderBookTest, RejectsNegativeLimitPrice)
{
    OrderBook book;

    EXPECT_FALSE(
        book.add_order({1, "AAPL", Side::BUY, -15000, 10})
    );

    EXPECT_EQ(book.total_orders(), 0);
}
TEST(OrderBookTest, ReturnsBestBidAndBestAsk)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::BUY, 15000, 10});
    book.add_order({2, "AAPL", Side::BUY, 15500, 10});

    book.add_order({3, "AAPL", Side::SELL, 16000, 10});
    book.add_order({4, "AAPL", Side::SELL, 15800, 10});

    EXPECT_EQ(book.best_bid(), 15500);
    EXPECT_EQ(book.best_ask(), 15800);
}
TEST(OrderBookTest, CalculatesBidAskSpread)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::BUY, 15000, 10});
    book.add_order({2, "AAPL", Side::SELL, 15500, 10});

    EXPECT_DOUBLE_EQ(book.get_bid_ask_spread(), 5.0);
}

TEST(OrderBookTest, MatchesMultiplePriceLevels)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::SELL, 15000, 5});
    book.add_order({2, "AAPL", Side::SELL, 15100, 5});
    book.add_order({3, "AAPL", Side::BUY, 15100, 8});

    ASSERT_EQ(book.get_trades().size(), 2);
    EXPECT_EQ(book.get_trades()[0].quantity, 5);
    EXPECT_EQ(book.get_trades()[1].quantity, 3);
}

TEST(OrderBookTest, ModifiesExistingOrder)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::BUY, 15000, 10});

    EXPECT_TRUE(book.modify_order(1, 15500, 20));
    EXPECT_EQ(book.remaining_quantity(1), 20);
}

TEST(OrderBookTest, ModifiedOrderCanBeMatched)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::SELL, 15000, 10});
    book.add_order({2, "AAPL", Side::BUY, 14000, 10});

    EXPECT_TRUE(book.modify_order(2, 15000, 10));

    ASSERT_EQ(book.get_trades().size(), 1);
    EXPECT_EQ(book.get_trades()[0].quantity, 10);
}

TEST(OrderBookTest, PreservesTradeHistory)
{
    OrderBook book;

    book.add_order({1, "AAPL", Side::SELL, 15000, 5});
    book.add_order({2, "AAPL", Side::BUY, 15000, 5});

    book.add_order({3, "AAPL", Side::SELL, 16000, 3});
    book.add_order({4, "AAPL", Side::BUY, 16000, 3});

    ASSERT_EQ(book.get_trades().size(), 2);
    EXPECT_EQ(book.get_trades()[0].quantity, 5);
    EXPECT_EQ(book.get_trades()[1].quantity, 3);
}
