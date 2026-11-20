# TradeForge

TradeForge is a C++20 stock-trading engine with a Qt 6 desktop terminal. It provides a local price-time matching engine, SQLite-backed trade history and order-book snapshots, and a dark Qt Widgets interface for order entry, market depth, historical execution charts, and a trade-based portfolio view.

TradeForge is a simulator and research project. It does not connect to a broker or provide live market data.

## Features

- Limit and market orders with price-time FIFO matching and partial fills
- Multiple isolated symbol order books
- Order cancellation and modification, duplicate-ID prevention, and order status tracking
- Trade history persisted in SQLite
- Transactional order-book snapshots with startup recovery
- Qt order-entry terminal with order-book, depth, spread, and recent-trade views
- Candlestick charts with OHLC and quantity volume at 1-minute, 5-minute, 15-minute, and 1-hour intervals
- Portfolio calculations using weighted-average cost basis, realized P&L, and unrealized P&L
- GoogleTest unit and integration tests

Chart candles are calculated from persisted executions. Portfolio positions include executed legs of orders submitted through the GUI and use the latest executed trade as the reference price. The portfolio does not represent a broker account or include fees, taxes, cash balances, or settlement.

## Architecture

- **Matching engine:** `OrderBook` owns price levels, FIFO queues, matching, and order status. `Exchange` routes orders by symbol and coordinates persistence.
- **Persistence:** `TradeRepository` stores executions and completed active-order snapshots in SQLite. The exchange restores the latest valid snapshot at startup.
- **Analytics:** `TradingAnalytics.hpp` builds interval candles and calculates portfolio positions without Qt dependencies.
- **Desktop UI:** `trading_terminal.cpp` implements the Qt Widgets application. `CandlestickChartWidget.hpp` paints the chart with `QPainter`; database reads and analytics run asynchronously.
- **CLI:** `src/main.cpp` remains a separate console executable.

## Technology

- C++20
- CMake 3.20 or newer
- Qt 6.12 or newer: Widgets and Concurrent
- SQLite 3
- GoogleTest 1.17, fetched by CMake FetchContent

## Build on macOS

Install Xcode Command Line Tools and the required Homebrew packages:

```bash
xcode-select --install
brew install cmake ninja qt sqlite
```

Configure and build from the repository root:

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build -j4
```

If CMake cannot locate Qt, point `CMAKE_PREFIX_PATH` to the Homebrew prefix containing `Qt6Config.cmake`, or set `Qt6_DIR` to the directory containing that file.

The first configure may need network access for CMake to fetch GoogleTest.

## Run

Run the command-line demo:

```bash
./build/trading_engine
```

Launch the Qt terminal:

```bash
./build/trading_terminal
```

Both executables use `trading_engine.db` in the current working directory. The database is local runtime data and is ignored by Git.

## Tests

Build and run all registered tests:

```bash
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

The suite covers matching and exchange behavior, SQLite trade persistence and schema migration, snapshot/recovery behavior, candle aggregation, and portfolio calculations.

## SQLite persistence and recovery

`TradeRepository` stores executions in `trades` and active-order snapshots in `order_snapshots` and `snapshot_orders`. New execution records contain a symbol, execution timestamp, and ownership flags for the buyer and seller order legs. Snapshot and trade writes use SQLite transactions. On startup, `Exchange` restores active orders from the latest completed, valid snapshot without matching them again.

The schema is migrated as the application opens the database. Older trades keep unknown timestamps and default to unowned legs; old snapshots restore orders as unowned. Existing executions are not assigned synthetic timestamps or retroactively attributed to the GUI.

## Charts and portfolio limitations

- There is no live quote or market-data feed. The portfolio labels its reference as the **last traded price**.
- Candles use actual execution prices and quantities. Older rows without execution timestamps cannot be placed into time buckets and are omitted from candles.
- Portfolio positions include only executed legs from orders entered through the GUI. CLI orders and older executions without ownership metadata are not attributed to the GUI portfolio.
- Cost basis uses weighted-average accounting. P&L excludes commissions, taxes, financing, and settlement effects.
- Market execution prices and portfolio reference prices are historical execution data, not current market valuations.

## Project structure

```text
include/                 Matching engine, exchange, repository, and analytics headers
src/main.cpp             CLI demo
src/trading_terminal.cpp Qt desktop application
src/CandlestickChartWidget.hpp
                          Qt-painted candlestick chart widget
src/benchmark.cpp        Benchmark source
tests/                   GoogleTest suites
CMakeLists.txt           Build targets and dependencies
README.md                Project documentation
```
