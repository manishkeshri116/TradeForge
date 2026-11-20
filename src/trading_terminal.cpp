#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QColor>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstdint>
#include <map>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

#include "Exchange.hpp"
#include "TradingAnalytics.hpp"
#include "CandlestickChartWidget.hpp"

namespace {

QString price_text(std::int64_t ticks)
{
    return QString::fromUtf8("₹%1").arg(static_cast<double>(ticks) / 100.0, 0, 'f', 2);
}

struct AnalyticsSnapshot
{
    std::vector<Candlestick> candles;
    std::vector<PortfolioPosition> positions;
    QString symbol;
    QString interval_label;
    std::size_t untimed_trade_count{0};
};

class TradingTerminal final : public QMainWindow
{
public:
    TradingTerminal()
        : repository_path_("trading_engine.db"), repository_(repository_path_), exchange_(repository_)
    {
        setWindowTitle("Trading Engine | Trading Terminal");
        resize(1440, 900);
        setMinimumSize(1120, 720);
        build_ui();
        refresh_views();
        connect(&refresh_timer_, &QTimer::timeout, this, [this] {
            refresh_views();
            poll_trade_history();
        });
        refresh_timer_.start(500);
        poll_trade_history();
        schedule_analytics();
    }

private:
    std::string repository_path_;
    TradeRepository repository_;
    Exchange exchange_;
    std::uint64_t next_order_id_{
        static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000};

    QLineEdit* symbol_input_{};
    QLineEdit* cancel_id_input_{};
    QComboBox* side_input_{};
    QComboBox* type_input_{};
    QDoubleSpinBox* price_input_{};
    QSpinBox* quantity_input_{};
    QLabel* selected_symbol_{};
    QLabel* best_bid_{};
    QLabel* best_ask_{};
    QLabel* spread_{};
    QLabel* active_count_{};
    QLabel* engine_status_{};
    QTableWidget* buy_table_{};
    QTableWidget* sell_table_{};
    QTableWidget* depth_table_{};
    QTableWidget* trades_table_{};
    QTimer refresh_timer_;
    QFutureWatcher<std::vector<TradeRecord>> history_watcher_;
    bool history_request_active_{false};
    std::int64_t last_trade_id_{0};
    std::map<std::int64_t, TradeRecord> trade_history_;
    std::set<QString> known_trade_symbols_;
    QComboBox* chart_symbol_input_{};
    QComboBox* chart_interval_input_{};
    CandlestickChartWidget* candlestick_chart_{};
    QTableWidget* portfolio_table_{};
    QLabel* portfolio_empty_{};
    QLabel* realized_total_{};
    QLabel* unrealized_total_{};
    QLabel* position_value_total_{};
    QFutureWatcher<AnalyticsSnapshot> analytics_watcher_;
    bool analytics_request_active_{false};
    bool analytics_dirty_{false};

    static QFrame* panel(const QString& title, QVBoxLayout*& layout)
    {
        auto* frame = new QFrame;
        frame->setObjectName("panel");
        layout = new QVBoxLayout(frame);
        layout->setContentsMargins(16, 14, 16, 16);
        layout->setSpacing(12);
        auto* heading = new QLabel(title);
        heading->setObjectName("panelTitle");
        layout->addWidget(heading);
        return frame;
    }

    static QTableWidget* make_table(const QStringList& headers)
    {
        auto* table = new QTableWidget(0, headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setAlternatingRowColors(true);
        table->setShowGrid(false);
        return table;
    }

    void build_ui()
    {
        setStyleSheet(R"(
            QMainWindow, QWidget { background: #0b0f14; color: #d7dee8; font-family: -apple-system, BlinkMacSystemFont, sans-serif; font-size: 13px; }
            QFrame#panel { background: #111821; border: 1px solid #202b38; border-radius: 10px; }
            QLabel#panelTitle { color: #eef3f8; font-size: 14px; font-weight: 700; }
            QLabel#appTitle { color: #f4f7fb; font-size: 20px; font-weight: 700; }
            QLabel#muted { color: #8391a2; }
            QLabel#metric { color: #eef3f8; font-size: 18px; font-weight: 700; }
            QLineEdit, QComboBox, QDoubleSpinBox, QSpinBox { background: #0c1219; border: 1px solid #2a3746; border-radius: 6px; padding: 8px; min-height: 20px; selection-background-color: #2563eb; }
            QComboBox::drop-down { border: 0; width: 22px; }
            QPushButton { background: #27364a; border: 0; border-radius: 6px; padding: 10px 12px; color: #e8eef6; font-weight: 650; }
            QPushButton:hover { background: #344964; }
            QPushButton#buyButton { background: #087f5b; }
            QPushButton#buyButton:hover { background: #099268; }
            QPushButton#sellButton { background: #b5454b; }
            QPushButton#sellButton:hover { background: #cf5057; }
            QTableWidget { background: #0d131a; alternate-background-color: #111923; border: 1px solid #202b38; border-radius: 6px; selection-background-color: #20354c; }
            QHeaderView::section { background: #17212c; color: #8796a8; border: 0; padding: 8px; font-weight: 650; }
            QTableWidget::item { padding: 6px; border: 0; }
            QSplitter::handle { background: #202b38; }
        )");

        auto* central = new QWidget;
        auto* root = new QVBoxLayout(central);
        root->setContentsMargins(18, 16, 18, 18);
        root->setSpacing(14);

        auto* header = new QHBoxLayout;
        auto* title = new QLabel("Trading Engine");
        title->setObjectName("appTitle");
        header->addWidget(title);
        header->addSpacing(18);
        engine_status_ = new QLabel("●  ENGINE ONLINE");
        engine_status_->setStyleSheet("color: #36c98f; font-weight: 700;");
        header->addWidget(engine_status_);
        header->addStretch();
        auto* selected_label = new QLabel("SYMBOL");
        selected_label->setObjectName("muted");
        header->addWidget(selected_label);
        selected_symbol_ = new QLabel("AAPL");
        selected_symbol_->setStyleSheet("font-size: 16px; font-weight: 700; color: #f4f7fb;");
        header->addWidget(selected_symbol_);
        root->addLayout(header);

        auto* body = new QHBoxLayout;
        body->setSpacing(14);

        QVBoxLayout* order_layout{};
        auto* order_panel = panel("ORDER ENTRY", order_layout);
        auto* form = new QFormLayout;
        form->setSpacing(10);
        side_input_ = new QComboBox;
        side_input_->addItems({"BUY", "SELL"});
        symbol_input_ = new QLineEdit("AAPL");
        symbol_input_->setMaxLength(16);
        type_input_ = new QComboBox;
        type_input_->addItems({"LIMIT", "MARKET"});
        price_input_ = new QDoubleSpinBox;
        price_input_->setRange(0.01, 100000000.00);
        price_input_->setDecimals(2);
        price_input_->setValue(150.00);
        price_input_->setPrefix("₹ ");
        quantity_input_ = new QSpinBox;
        quantity_input_->setRange(1, 1000000000);
        quantity_input_->setValue(10);
        form->addRow("Side", side_input_);
        form->addRow("Symbol", symbol_input_);
        form->addRow("Order type", type_input_);
        form->addRow("Limit price", price_input_);
        form->addRow("Quantity", quantity_input_);
        order_layout->addLayout(form);
        auto* submit = new QPushButton("Submit Order");
        submit->setObjectName("buyButton");
        order_layout->addWidget(submit);
        auto* separator = new QFrame;
        separator->setFrameShape(QFrame::HLine);
        separator->setStyleSheet("color: #263342;");
        order_layout->addWidget(separator);
        auto* cancel_label = new QLabel("CANCEL ORDER");
        cancel_label->setObjectName("muted");
        order_layout->addWidget(cancel_label);
        cancel_id_input_ = new QLineEdit;
        cancel_id_input_->setPlaceholderText("Order ID");
        order_layout->addWidget(cancel_id_input_);
        auto* cancel = new QPushButton("Cancel Order");
        order_layout->addWidget(cancel);
        order_layout->addStretch();
        order_panel->setMinimumWidth(250);
        order_panel->setMaximumWidth(310);
        body->addWidget(order_panel);

        QVBoxLayout* book_layout{};
        auto* book_panel = panel("ORDER BOOK", book_layout);
        auto* quotes = new QHBoxLayout;
        auto* bid_quote = new QLabel("BEST BID  —");
        bid_quote->setObjectName("muted");
        best_bid_ = new QLabel("—");
        best_bid_->setStyleSheet("color: #36c98f; font-weight: 700;");
        auto* ask_quote = new QLabel("BEST ASK  —");
        ask_quote->setObjectName("muted");
        best_ask_ = new QLabel("—");
        best_ask_->setStyleSheet("color: #f07878; font-weight: 700;");
        quotes->addWidget(bid_quote);
        quotes->addWidget(best_bid_);
        quotes->addSpacing(20);
        quotes->addWidget(ask_quote);
        quotes->addWidget(best_ask_);
        quotes->addStretch();
        book_layout->addLayout(quotes);
        auto* book_split = new QSplitter(Qt::Vertical);
        auto* asks_container = new QWidget;
        auto* asks_layout = new QVBoxLayout(asks_container);
        asks_layout->setContentsMargins(0, 0, 0, 0);
        auto* asks_title = new QLabel("SELL ORDERS");
        asks_title->setStyleSheet("color: #f07878; font-weight: 700;");
        asks_layout->addWidget(asks_title);
        sell_table_ = make_table({"Order ID", "Price", "Remaining", "Status"});
        asks_layout->addWidget(sell_table_);
        auto* bids_container = new QWidget;
        auto* bids_layout = new QVBoxLayout(bids_container);
        bids_layout->setContentsMargins(0, 0, 0, 0);
        auto* bids_title = new QLabel("BUY ORDERS");
        bids_title->setStyleSheet("color: #36c98f; font-weight: 700;");
        bids_layout->addWidget(bids_title);
        buy_table_ = make_table({"Order ID", "Price", "Remaining", "Status"});
        bids_layout->addWidget(buy_table_);
        book_split->addWidget(asks_container);
        book_split->addWidget(bids_container);
        book_split->setStretchFactor(0, 1);
        book_split->setStretchFactor(1, 1);
        book_layout->addWidget(book_split);
        body->addWidget(book_panel, 1);

        QVBoxLayout* info_layout{};
        auto* info_panel = panel("MARKET INFORMATION", info_layout);
        auto* spread_title = new QLabel("BID–ASK SPREAD");
        spread_title->setObjectName("muted");
        spread_ = new QLabel("—");
        spread_->setObjectName("metric");
        auto* count_title = new QLabel("ACTIVE ORDERS");
        count_title->setObjectName("muted");
        active_count_ = new QLabel("0");
        active_count_->setObjectName("metric");
        info_layout->addWidget(spread_title);
        info_layout->addWidget(spread_);
        info_layout->addSpacing(6);
        info_layout->addWidget(count_title);
        info_layout->addWidget(active_count_);
        info_layout->addSpacing(8);
        auto* depth_title = new QLabel("MARKET DEPTH");
        depth_title->setObjectName("muted");
        info_layout->addWidget(depth_title);
        depth_table_ = make_table({"Side", "Price", "Orders", "Quantity"});
        info_layout->addWidget(depth_table_, 1);
        info_panel->setMinimumWidth(250);
        info_panel->setMaximumWidth(330);
        body->addWidget(info_panel);
        QVBoxLayout* trades_layout{};
        auto* trades_panel = panel("RECENT TRADES", trades_layout);
        trades_table_ = make_table({"Symbol", "Buy Order ID", "Sell Order ID", "Execution Price", "Quantity"});
        trades_table_->setMaximumHeight(180);
        trades_layout->addWidget(trades_table_);

        auto* tabs = new QTabWidget;
        auto* trading_page = new QWidget;
        auto* trading_layout = new QVBoxLayout(trading_page);
        trading_layout->setContentsMargins(0, 10, 0, 0);
        trading_layout->addLayout(body, 1);
        trading_layout->addWidget(trades_panel);
        tabs->addTab(trading_page, "Trading");

        auto* charts_page = new QWidget;
        auto* charts_layout = new QVBoxLayout(charts_page);
        charts_layout->setContentsMargins(0, 14, 0, 0);
        auto* chart_controls = new QHBoxLayout;
        auto* chart_symbol_label = new QLabel("SYMBOL");
        chart_symbol_label->setObjectName("muted");
        chart_symbol_input_ = new QComboBox;
        chart_symbol_input_->setMinimumWidth(150);
        chart_symbol_input_->addItem("AAPL");
        auto* interval_label = new QLabel("INTERVAL");
        interval_label->setObjectName("muted");
        chart_interval_input_ = new QComboBox;
        chart_interval_input_->addItem("1 minute", 60'000);
        chart_interval_input_->addItem("5 minutes", 300'000);
        chart_interval_input_->addItem("15 minutes", 900'000);
        chart_interval_input_->addItem("1 hour", 3'600'000);
        chart_controls->addWidget(chart_symbol_label);
        chart_controls->addWidget(chart_symbol_input_);
        chart_controls->addSpacing(12);
        chart_controls->addWidget(interval_label);
        chart_controls->addWidget(chart_interval_input_);
        chart_controls->addStretch();
        auto* chart_note = new QLabel("OHLC and volume are derived from persisted executions");
        chart_note->setObjectName("muted");
        chart_controls->addWidget(chart_note);
        charts_layout->addLayout(chart_controls);
        candlestick_chart_ = new CandlestickChartWidget;
        charts_layout->addWidget(candlestick_chart_, 1);
        tabs->addTab(charts_page, "Charts");

        auto* portfolio_page = new QWidget;
        auto* portfolio_layout = new QVBoxLayout(portfolio_page);
        portfolio_layout->setContentsMargins(0, 14, 0, 0);
        auto* summary_cards = new QHBoxLayout;
        const auto add_summary_card = [summary_cards](const QString& title, QLabel*& value) {
            QVBoxLayout* card_layout{};
            auto* card = panel(title, card_layout);
            value = new QLabel("₹0.00");
            value->setObjectName("metric");
            card_layout->addWidget(value);
            summary_cards->addWidget(card);
        };
        add_summary_card("REALIZED P&L", realized_total_);
        add_summary_card("UNREALIZED P&L · LAST TRADE", unrealized_total_);
        add_summary_card("NET POSITION VALUE · LAST TRADE", position_value_total_);
        portfolio_layout->addLayout(summary_cards);
        auto* ownership_note = new QLabel(
            "Portfolio includes execution legs from orders entered in this terminal. Older orders without ownership metadata are not attributed.");
        ownership_note->setObjectName("muted");
        portfolio_layout->addWidget(ownership_note);
        portfolio_empty_ = new QLabel("No GUI-owned executions yet. Portfolio positions are built from trades of orders entered in this terminal.");
        portfolio_empty_->setObjectName("muted");
        portfolio_layout->addWidget(portfolio_empty_);
        portfolio_table_ = make_table({"Symbol", "Net Qty", "Average Entry", "Last Traded Price", "Position Value", "Realized P&L", "Unrealized P&L"});
        portfolio_layout->addWidget(portfolio_table_, 1);
        tabs->addTab(portfolio_page, "Portfolio");

        root->addWidget(tabs, 1);
        setCentralWidget(central);

        connect(symbol_input_, &QLineEdit::textChanged, this, [this] { refresh_views(); });
        connect(symbol_input_, &QLineEdit::editingFinished, this, [this] {
            sync_chart_symbols();
            schedule_analytics();
        });
        connect(type_input_, &QComboBox::currentTextChanged, this, [this](const QString& type) {
            price_input_->setEnabled(type == "LIMIT");
        });
        connect(side_input_, &QComboBox::currentTextChanged, this, [submit](const QString& side) {
            submit->setObjectName(side == "BUY" ? "buyButton" : "sellButton");
            submit->style()->unpolish(submit);
            submit->style()->polish(submit);
        });
        connect(submit, &QPushButton::clicked, this, [this] { submit_order(); });
        connect(cancel, &QPushButton::clicked, this, [this] { cancel_order(); });
        connect(chart_symbol_input_, &QComboBox::currentTextChanged, this,
                [this] { schedule_analytics(); });
        connect(chart_interval_input_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this] { schedule_analytics(); });
        connect(&history_watcher_, &QFutureWatcher<std::vector<TradeRecord>>::finished,
                this, [this] { finish_trade_history_poll(); });
        connect(&analytics_watcher_, &QFutureWatcher<AnalyticsSnapshot>::finished,
                this, [this] { finish_analytics(); });
    }

    static void set_cell_text(QTableWidget* table, int row, int column,
                              const QString& text, const QColor& color = QColor())
    {
        auto* item = table->item(row, column);
        if (item == nullptr) {
            item = new QTableWidgetItem;
            table->setItem(row, column, item);
        }
        if (item->text() != text) item->setText(text);
        if (color.isValid()) item->setForeground(color);
    }

    void fill_orders(QTableWidget* table, const std::vector<Order>& orders, Side side)
    {
        std::vector<Order> filtered;
        std::copy_if(orders.begin(), orders.end(), std::back_inserter(filtered),
                     [side](const Order& order) { return order.side == side; });
        std::sort(filtered.begin(), filtered.end(), [side](const Order& a, const Order& b) {
            if (a.price_ticks != b.price_ticks)
                return side == Side::BUY ? a.price_ticks > b.price_ticks : a.price_ticks < b.price_ticks;
            return a.timestamp < b.timestamp;
        });
        table->setRowCount(static_cast<int>(filtered.size()));
        for (int row = 0; row < static_cast<int>(filtered.size()); ++row) {
            const auto& order = filtered[static_cast<std::size_t>(row)];
            const QColor side_color = side == Side::BUY ? QColor("#36c98f") : QColor("#f07878");
            const QString status = QString::fromStdString(get_status_name(order.status));
            set_cell_text(table, row, 0, QString::number(order.order_id), side_color);
            set_cell_text(table, row, 1, price_text(order.price_ticks), side_color);
            set_cell_text(table, row, 2, QString::number(order.quantity));
            set_cell_text(table, row, 3, status,
                          order.status == OrderStatus::PARTIALLY_FILLED
                              ? QColor("#f0b85b") : QColor("#a7b4c3"));
        }
    }

    void refresh_views()
    {
        const std::string symbol = symbol_input_->text().trimmed().toUpper().toStdString();
        const QString symbol_label = symbol.empty() ? "—" : QString::fromStdString(symbol);
        if (selected_symbol_->text() != symbol_label) selected_symbol_->setText(symbol_label);
        const auto orders = exchange_.get_active_orders(symbol);
        fill_orders(sell_table_, orders, Side::SELL);
        fill_orders(buy_table_, orders, Side::BUY);
        const QString bid_text = exchange_.best_bid(symbol) ? price_text(exchange_.best_bid(symbol)) : "—";
        const QString ask_text = exchange_.best_ask(symbol) ? price_text(exchange_.best_ask(symbol)) : "—";
        const QString count_text = QString::number(exchange_.get_order_count(symbol));
        if (best_bid_->text() != bid_text) best_bid_->setText(bid_text);
        if (best_ask_->text() != ask_text) best_ask_->setText(ask_text);
        if (active_count_->text() != count_text) active_count_->setText(count_text);
        if (exchange_.best_bid(symbol) && exchange_.best_ask(symbol)) {
            const auto spread_ticks = exchange_.best_ask(symbol) - exchange_.best_bid(symbol);
            const QString text = price_text(spread_ticks);
            if (spread_->text() != text) spread_->setText(text);
        } else {
            if (spread_->text() != "—") spread_->setText("—");
        }

        std::map<std::int64_t, std::pair<int, std::uint64_t>, std::greater<>> bids;
        std::map<std::int64_t, std::pair<int, std::uint64_t>> asks;
        for (const auto& order : orders) {
            if (order.side == Side::BUY) {
                auto& level = bids[order.price_ticks];
                ++level.first;
                level.second += order.quantity;
            } else {
                auto& level = asks[order.price_ticks];
                ++level.first;
                level.second += order.quantity;
            }
        }
        const int depth_count = static_cast<int>(bids.size() + asks.size());
        depth_table_->setRowCount(depth_count);
        int row = 0;
        const auto add_depth = [this, &row](const auto& levels, const QString& side) {
            for (const auto& [price, aggregate] : levels) {
                const QColor side_color = side == "BUY" ? QColor("#36c98f") : QColor("#f07878");
                set_cell_text(depth_table_, row, 0, side, side_color);
                set_cell_text(depth_table_, row, 1, price_text(price), side_color);
                set_cell_text(depth_table_, row, 2, QString::number(aggregate.first));
                set_cell_text(depth_table_, row, 3, QString::number(aggregate.second));
                ++row;
            }
        };
        add_depth(asks, "SELL");
        add_depth(bids, "BUY");

        render_trade_history();
    }

    void render_trade_history()
    {
        const auto visible_count = std::min<std::size_t>(trade_history_.size(), 100);
        trades_table_->setRowCount(static_cast<int>(visible_count));
        auto record = trade_history_.rbegin();
        for (std::size_t i = 0; i < visible_count; ++i, ++record) {
            const auto& trade = record->second.trade;
            const int row = static_cast<int>(i);
            set_cell_text(trades_table_, row, 0,
                          record->second.symbol.empty() ? "LEGACY" : QString::fromStdString(record->second.symbol));
            set_cell_text(trades_table_, row, 1, QString::number(trade.buy_order_id), QColor("#36c98f"));
            set_cell_text(trades_table_, row, 2, QString::number(trade.sell_order_id), QColor("#f07878"));
            set_cell_text(trades_table_, row, 3, price_text(trade.price_ticks));
            set_cell_text(trades_table_, row, 4, QString::number(trade.quantity));
        }
    }

    void poll_trade_history()
    {
        if (history_request_active_) return;
        history_request_active_ = true;
        const std::string path = repository_path_;
        const std::int64_t after_id = last_trade_id_;
        history_watcher_.setFuture(QtConcurrent::run([path, after_id] {
            TradeRepository reader(path, true);
            return reader.get_trade_records_after(after_id, 500);
        }));
    }

    void finish_trade_history_poll()
    {
        history_request_active_ = false;
        try {
            const auto records = history_watcher_.result();
            for (const auto& record : records) {
                if (!record.symbol.empty())
                    known_trade_symbols_.insert(QString::fromStdString(record.symbol));
                trade_history_.try_emplace(record.id, record);
                last_trade_id_ = std::max(last_trade_id_, record.id);
            }
            while (trade_history_.size() > 100)
                trade_history_.erase(trade_history_.begin());
            if (!records.empty()) {
                render_trade_history();
                sync_chart_symbols();
                schedule_analytics();
            }
            if (records.size() == 500)
                QTimer::singleShot(0, this, [this] { poll_trade_history(); });
        } catch (const std::exception& error) {
            statusBar()->showMessage(QString("Trade history refresh failed: %1").arg(error.what()), 5000);
        }
    }

    void sync_chart_symbols()
    {
        std::set<QString> symbols = known_trade_symbols_;
        const auto selected = symbol_input_->text().trimmed().toUpper();
        if (!selected.isEmpty()) symbols.insert(selected);
        const QString current = chart_symbol_input_->currentText();
        QSignalBlocker blocker(chart_symbol_input_);
        chart_symbol_input_->clear();
        for (const auto& symbol : symbols) chart_symbol_input_->addItem(symbol);
        const int current_index = chart_symbol_input_->findText(current);
        if (current_index >= 0) chart_symbol_input_->setCurrentIndex(current_index);
        else if (!selected.isEmpty()) chart_symbol_input_->setCurrentText(selected);
    }

    void schedule_analytics()
    {
        if (analytics_request_active_) {
            analytics_dirty_ = true;
            return;
        }
        analytics_request_active_ = true;
        analytics_dirty_ = false;
        const std::string path = repository_path_;
        const QString symbol = chart_symbol_input_->currentText();
        const QString interval_label = chart_interval_input_->currentText();
        const auto interval_ms = chart_interval_input_->currentData().toLongLong();
        analytics_watcher_.setFuture(QtConcurrent::run(
            [path, symbol, interval_label, interval_ms] {
                TradeRepository reader(path, true);
                const auto records = reader.get_trade_records_after(
                    0, std::numeric_limits<std::size_t>::max());
                AnalyticsSnapshot result;
                result.symbol = symbol;
                result.interval_label = interval_label;
                result.candles = build_candlesticks(
                    records, symbol.toStdString(), std::chrono::milliseconds(interval_ms));
                if (result.candles.size() > 300)
                    result.candles.erase(result.candles.begin(), result.candles.end() - 300);
                result.positions = calculate_portfolio(records);
                result.untimed_trade_count = static_cast<std::size_t>(std::count_if(
                    records.begin(), records.end(), [&symbol](const TradeRecord& record) {
                        return record.symbol == symbol.toStdString() &&
                               !record.trade.executed_at.has_value();
                    }));
                return result;
            }));
    }

    void finish_analytics()
    {
        analytics_request_active_ = false;
        try {
            const auto result = analytics_watcher_.result();
            const QString selected_symbol = chart_symbol_input_->currentText();
            const QString selected_interval = chart_interval_input_->currentText();
            if (result.symbol == selected_symbol && result.interval_label == selected_interval) {
                candlestick_chart_->set_data(result.candles, result.symbol,
                                             result.interval_label, result.untimed_trade_count);
                render_portfolio(result.positions);
            } else {
                analytics_dirty_ = true;
            }
        } catch (const std::exception& error) {
            statusBar()->showMessage(QString("Analytics refresh failed: %1").arg(error.what()), 5000);
        }
        if (analytics_dirty_) schedule_analytics();
    }

    void render_portfolio(const std::vector<PortfolioPosition>& positions)
    {
        portfolio_empty_->setVisible(positions.empty());
        portfolio_table_->setRowCount(static_cast<int>(positions.size()));
        double realized_total = 0;
        double unrealized_total = 0;
        double value_total = 0;
        const auto money = [](double value) {
            return QString::fromUtf8("₹%1").arg(value, 0, 'f', 2);
        };
        const auto pnl_color = [](double value) {
            return value > 0 ? QColor("#36c98f")
                 : value < 0 ? QColor("#f07878") : QColor("#a7b4c3");
        };
        for (int row = 0; row < static_cast<int>(positions.size()); ++row) {
            const auto& position = positions[static_cast<std::size_t>(row)];
            realized_total += position.realized_pnl;
            unrealized_total += position.unrealized_pnl;
            value_total += position.position_value;
            set_cell_text(portfolio_table_, row, 0, QString::fromStdString(position.symbol));
            set_cell_text(portfolio_table_, row, 1, QString::number(position.net_quantity),
                          pnl_color(static_cast<double>(position.net_quantity)));
            set_cell_text(portfolio_table_, row, 2, money(position.average_entry_ticks / 100.0));
            set_cell_text(portfolio_table_, row, 3, price_text(position.last_trade_price_ticks));
            set_cell_text(portfolio_table_, row, 4, money(position.position_value));
            set_cell_text(portfolio_table_, row, 5, money(position.realized_pnl), pnl_color(position.realized_pnl));
            set_cell_text(portfolio_table_, row, 6, money(position.unrealized_pnl), pnl_color(position.unrealized_pnl));
        }
        realized_total_->setText(money(realized_total));
        unrealized_total_->setText(money(unrealized_total));
        position_value_total_->setText(money(value_total));
        realized_total_->setStyleSheet(QString("font-size: 18px; font-weight: 700; color: %1;")
            .arg(pnl_color(realized_total).name()));
        unrealized_total_->setStyleSheet(QString("font-size: 18px; font-weight: 700; color: %1;")
            .arg(pnl_color(unrealized_total).name()));
        position_value_total_->setStyleSheet(QString("font-size: 18px; font-weight: 700; color: %1;")
            .arg(pnl_color(value_total).name()));
    }

    void submit_order()
    {
        const std::string symbol = symbol_input_->text().trimmed().toUpper().toStdString();
        if (symbol.empty()) {
            QMessageBox::warning(this, "Invalid order", "Enter a trading symbol.");
            return;
        }
        const Side side = side_input_->currentText() == "BUY" ? Side::BUY : Side::SELL;
        const OrderType type = type_input_->currentText() == "LIMIT" ? OrderType::LIMIT : OrderType::MARKET;
        const auto price_ticks = type == OrderType::LIMIT
            ? static_cast<std::int64_t>(price_input_->value() * 100.0 + 0.5) : 0;
        Order order{next_order_id_++, symbol, side, price_ticks,
                    static_cast<std::uint64_t>(quantity_input_->value()), type};
        order.portfolio_owned = true;
        try {
            if (!exchange_.add_order(order)) {
                QMessageBox::warning(this, "Order rejected", "The order was invalid or its ID was already used.");
                return;
            }
        } catch (const std::exception& error) {
            QMessageBox::critical(this, "Order error", QString::fromUtf8(error.what()));
            return;
        }
        refresh_views();
        statusBar()->showMessage(QString("%1 order accepted · ID %2")
            .arg(side_input_->currentText()).arg(order.order_id), 4000);
    }

    void cancel_order()
    {
        bool ok = false;
        const auto order_id = cancel_id_input_->text().trimmed().toULongLong(&ok);
        const std::string symbol = symbol_input_->text().trimmed().toUpper().toStdString();
        if (!ok || order_id == 0) {
            QMessageBox::warning(this, "Invalid order ID", "Enter a valid active order ID to cancel.");
            return;
        }
        try {
            if (!exchange_.cancel_order(symbol, order_id)) {
                QMessageBox::information(this, "Order not found", "No active order with that ID exists for this symbol.");
                return;
            }
        } catch (const std::exception& error) {
            QMessageBox::critical(this, "Cancellation error", QString::fromUtf8(error.what()));
            return;
        }
        cancel_id_input_->clear();
        refresh_views();
        statusBar()->showMessage(QString("Order %1 cancelled").arg(order_id), 4000);
    }
};

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    try {
        TradingTerminal terminal;
        terminal.show();
        return app.exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr, "Startup error", QString::fromUtf8(error.what()));
        return 1;
    }
}
