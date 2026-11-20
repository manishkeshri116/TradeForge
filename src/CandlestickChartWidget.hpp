#pragma once

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "TradingAnalytics.hpp"

class CandlestickChartWidget final : public QWidget
{
public:
    explicit CandlestickChartWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(360);
        setMouseTracking(true);
        setToolTipDuration(5000);
    }

    void set_data(std::vector<Candlestick> candles, QString symbol,
                  QString interval_label, std::size_t untimed_trade_count)
    {
        candles_ = std::move(candles);
        symbol_ = std::move(symbol);
        interval_label_ = std::move(interval_label);
        untimed_trade_count_ = untimed_trade_count;
        hovered_index_ = -1;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor("#0d131a"));

        constexpr int left = 76;
        constexpr int right = 22;
        constexpr int top = 24;
        constexpr int bottom = 35;
        constexpr double volume_fraction = 0.22;
        const QRectF full_area(left, top, std::max(1, width() - left - right),
                               std::max(1, height() - top - bottom));
        const qreal volume_height = full_area.height() * volume_fraction;
        const QRectF price_area(full_area.left(), full_area.top(), full_area.width(),
                                full_area.height() - volume_height - 12);
        const QRectF volume_area(full_area.left(), price_area.bottom() + 12,
                                 full_area.width(), volume_height);

        if (candles_.empty()) {
            painter.setPen(QColor("#8796a8"));
            QString message = "Insufficient timestamped trades to build candles for " + symbol_ + ".";
            if (untimed_trade_count_ > 0)
                message += QString(" %1 legacy trade(s) have no execution timestamp.").arg(untimed_trade_count_);
            painter.drawText(price_area, Qt::AlignCenter | Qt::TextWordWrap, message);
            return;
        }

        const std::size_t display_count = std::min<std::size_t>(candles_.size(), 300);
        const std::size_t first = candles_.size() - display_count;
        std::int64_t min_price = candles_[first].low_ticks;
        std::int64_t max_price = candles_[first].high_ticks;
        std::uint64_t max_volume = 1;
        for (std::size_t i = first; i < candles_.size(); ++i) {
            min_price = std::min(min_price, candles_[i].low_ticks);
            max_price = std::max(max_price, candles_[i].high_ticks);
            max_volume = std::max(max_volume, candles_[i].volume);
        }
        if (max_price == min_price) {
            min_price -= std::max<std::int64_t>(1, std::abs(min_price) / 100);
            max_price += std::max<std::int64_t>(1, std::abs(max_price) / 100);
        }

        painter.setFont(QFont("Helvetica Neue", 9));
        painter.setPen(QColor("#a8b5c5"));
        painter.drawText(QPointF(price_area.left(), 15),
                         symbol_ + " · " + interval_label_ + " · EXECUTED TRADES");
        for (int grid = 0; grid <= 4; ++grid) {
            const qreal y = price_area.top() + price_area.height() * grid / 4.0;
            painter.setPen(QPen(QColor("#202b38"), 1, Qt::DashLine));
            painter.drawLine(QPointF(price_area.left(), y), QPointF(price_area.right(), y));
            const double ratio = 1.0 - grid / 4.0;
            const double value = (min_price + (max_price - min_price) * ratio) / 100.0;
            painter.setPen(QColor("#8b98a8"));
            painter.drawText(QRectF(0, y - 9, left - 9, 18), Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(value, 'f', 2));
        }

        const qreal slot = price_area.width() / static_cast<qreal>(display_count);
        const qreal body_width = std::clamp(slot * 0.62, 2.0, 14.0);
        for (std::size_t i = 0; i < display_count; ++i) {
            const std::size_t candle_index = first + i;
            const auto& candle = candles_[candle_index];
            const qreal center_x = price_area.left() + (static_cast<qreal>(i) + 0.5) * slot;
            const auto y_for = [&](std::int64_t value) {
                const double ratio = static_cast<double>(max_price - value) /
                                     static_cast<double>(max_price - min_price);
                return price_area.top() + ratio * price_area.height();
            };
            const bool up = candle.close_ticks >= candle.open_ticks;
            const QColor candle_color = up ? QColor("#2ec98b") : QColor("#f06b73");
            painter.setPen(QPen(candle_color, 1.2));
            painter.drawLine(QPointF(center_x, y_for(candle.high_ticks)),
                             QPointF(center_x, y_for(candle.low_ticks)));
            const qreal open_y = y_for(candle.open_ticks);
            const qreal close_y = y_for(candle.close_ticks);
            const QRectF body(center_x - body_width / 2, std::min(open_y, close_y),
                              body_width, std::max<qreal>(2, std::abs(close_y - open_y)));
            painter.fillRect(body, candle_color);

            const qreal bar_height = volume_area.height() *
                static_cast<double>(candle.volume) / static_cast<double>(max_volume);
            painter.fillRect(QRectF(center_x - body_width / 2,
                                    volume_area.bottom() - bar_height,
                                    body_width, bar_height),
                             up ? QColor("#1e765d") : QColor("#8f4148"));
            if (static_cast<int>(i) == hovered_index_) {
                painter.setPen(QPen(QColor("#9fb0c3"), 1, Qt::DashLine));
                painter.drawLine(QPointF(center_x, price_area.top()),
                                 QPointF(center_x, volume_area.bottom()));
            }
        }

        painter.setPen(QColor("#8b98a8"));
        const auto first_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            candles_[first].start_time.time_since_epoch()).count();
        const auto last_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            candles_.back().start_time.time_since_epoch()).count();
        painter.drawText(QRectF(price_area.left(), volume_area.bottom() + 5,
                                price_area.width() / 2, bottom - 5), Qt::AlignLeft | Qt::AlignTop,
                         QDateTime::fromMSecsSinceEpoch(first_ms).toString("yyyy-MM-dd HH:mm"));
        painter.drawText(QRectF(price_area.center().x(), volume_area.bottom() + 5,
                                price_area.width() / 2, bottom - 5), Qt::AlignRight | Qt::AlignTop,
                         QDateTime::fromMSecsSinceEpoch(last_ms).toString("yyyy-MM-dd HH:mm"));
        painter.drawText(QRectF(volume_area.left(), volume_area.top(), 54, 14),
                         Qt::AlignLeft | Qt::AlignTop, "VOLUME");
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        constexpr int left = 76;
        constexpr int right = 22;
        const qreal chart_width = std::max(1, width() - left - right);
        if (!candles_.empty() && event->position().x() >= left && event->position().x() <= width() - right) {
            const std::size_t display_count = std::min<std::size_t>(candles_.size(), 300);
            const std::size_t first = candles_.size() - display_count;
            const auto slot = chart_width / static_cast<qreal>(display_count);
            hovered_index_ = std::clamp(static_cast<int>((event->position().x() - left) / slot),
                                        0, static_cast<int>(display_count) - 1);
            const auto& candle = candles_[first + static_cast<std::size_t>(hovered_index_)];
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                candle.start_time.time_since_epoch()).count();
            const QString details = QString("%1 · %2 · %3\nO %4  H %5  L %6  C %7\nVolume %8  Trades %9")
                .arg(symbol_, interval_label_, QDateTime::fromMSecsSinceEpoch(millis).toString("yyyy-MM-dd HH:mm"))
                .arg(candle.open_ticks / 100.0, 0, 'f', 2)
                .arg(candle.high_ticks / 100.0, 0, 'f', 2)
                .arg(candle.low_ticks / 100.0, 0, 'f', 2)
                .arg(candle.close_ticks / 100.0, 0, 'f', 2)
                .arg(candle.volume).arg(candle.trade_count);
            QToolTip::showText(event->globalPosition().toPoint(), details, this);
        } else {
            hovered_index_ = -1;
            QToolTip::hideText();
        }
        update();
    }

    void leaveEvent(QEvent*) override
    {
        hovered_index_ = -1;
        QToolTip::hideText();
        update();
    }

private:
    std::vector<Candlestick> candles_;
    QString symbol_;
    QString interval_label_;
    std::size_t untimed_trade_count_{0};
    int hovered_index_{-1};
};
