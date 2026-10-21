/**
 * EdgeEngine - Example Strategies
 *
 * Demonstrates the general-purpose MT5 Strategy Tester API.
 * Each class is a self-contained strategy - none of these know about or
 * depend on any indicator code in the engine itself.
 *
 * Example strategies:
 *  1. SmaCrossStrategy  - Classic SMA crossover (fast/slow EMA)
 *  2. RSIStrategy       - RSI mean-reversion (buy oversold, sell overbought)
 *  3. BollingerBandStrategy - Bollinger band breakout + reversal
 *  4. GridStrategy      - Fixed-interval grid trading
 *
 * Usage from C++:
 *   #include "tester.hpp"
 *   #include "examples/strategies.hpp"
 *
 *   SmaCrossStrategy strat;
 *   strat.fast_period = 10;
 *   strat.slow_period = 30;
 *
 *   Tester tester;
 *   TesterResult res = tester.run(&strat, bars, n, sym, cfg);
 */
#pragma once

#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>
#include "../core/include/strategy_base.hpp"

namespace edge {
namespace examples {

// ============================================================
// Utility: Compute SMA over last `period` bars from current bar
// ============================================================
inline double sma_close(const TradeContext& T, int period) {
    if (T.BarsCount() < period) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < period; ++i) sum += T.iClose(i);
    return sum / period;
}

// Compute EMA using Wilder / standard multiplier
inline double ema_close(const TradeContext& T, int period, double& prev_ema, bool& initialized) {
    double price = T.iClose(0);
    if (!initialized) {
        // Seed with SMA if enough bars
        if (T.BarsCount() < period) return price;
        double sum = 0.0;
        for (int i = 0; i < period; ++i) sum += T.iClose(i);
        prev_ema = sum / period;
        initialized = true;
        return prev_ema;
    }
    double k  = 2.0 / (period + 1.0);
    prev_ema  = prev_ema + k * (price - prev_ema);
    return prev_ema;
}

// Compute ATR over last `period` bars
inline double atr(const TradeContext& T, int period) {
    if (T.BarsCount() < period + 1) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < period; ++i) {
        double h  = T.iHigh(i);
        double l  = T.iLow(i);
        double pc = T.iClose(i + 1);
        double tr = std::max({h - l, std::abs(h - pc), std::abs(l - pc)});
        sum += tr;
    }
    return sum / period;
}


// ============================================================
// 1. SmaCrossStrategy
//    Buy when fast EMA crosses above slow EMA.
//    Sell (short) when fast EMA crosses below slow EMA.
//    SL = ATR * sl_atr_mult from entry price.
// ============================================================
class SmaCrossStrategy : public Strategy {
public:
    int    fast_period   = 10;
    int    slow_period   = 30;
    int    atr_period    = 14;
    double sl_atr_mult   = 2.0;
    double risk_pct      = 1.0;   // % of balance risked per trade
    bool   allow_short   = true;

    int OnInit() override {
        fast_ema_ = 0.0;
        slow_ema_ = 0.0;
        fast_init_ = slow_init_ = false;
        return INIT_SUCCEEDED;
    }

    void OnBar() override {
        if (!Trade || Trade->BarsCount() < slow_period + 2) return;

        double fast = ema_close(*Trade, fast_period, fast_ema_, fast_init_);
        double slow = ema_close(*Trade, slow_period, slow_ema_, slow_init_);
        if (!fast_init_ || !slow_init_) return;

        double prev_close = Trade->iClose(1);
        double cur_close  = Trade->iClose(0);
        double cur_atr    = atr(*Trade, atr_period);
        if (cur_atr <= 0.0) return;

        // Detect crossover: compare current and previous bar's EMA diff sign
        // Use close-based proxy since we only store one EMA value
        bool cross_up   = (cur_close > fast) && (prev_close <= fast) && (fast > slow);
        bool cross_down = (cur_close < fast) && (prev_close >= fast) && (fast < slow);

        int pos_count = Trade->PositionsTotal();

        // Close any open positions on opposite signal
        if (cross_up || cross_down) {
            for (const auto& pos : Trade->Positions()) {
                bool is_buy  = (pos.type == POSITION_TYPE_BUY);
                if (cross_up && !is_buy)  Trade->PositionClose(pos.ticket);
                if (cross_down && is_buy) Trade->PositionClose(pos.ticket);
            }
        }

        // Entry
        double sl_dist = cur_atr * sl_atr_mult;
        double lots = _risk_lots(sl_dist);
        if (lots <= 0.0) return;

        if (cross_up && Trade->PositionsTotal() == 0) {
            double sl = Trade->Ask() - sl_dist;
            Trade->Buy(lots, sl, 0.0, "SmaCross_Buy");
        } else if (cross_down && allow_short && Trade->PositionsTotal() == 0) {
            double sl = Trade->Bid() + sl_dist;
            Trade->Sell(lots, sl, 0.0, "SmaCross_Sell");
        }
    }

private:
    double fast_ema_, slow_ema_;
    bool   fast_init_, slow_init_;

    double _risk_lots(double sl_dist) const {
        if (sl_dist <= 1e-10) return ctx.symbol.volume_min;
        double balance   = ctx.account->balance();
        double risk_amt  = balance * (risk_pct / 100.0);
        double pts_sl    = sl_dist / ctx.symbol.point;
        double val_pt    = ctx.symbol.trade_tick_value * (ctx.symbol.point / ctx.symbol.trade_tick_size);
        if (pts_sl * val_pt <= 0.0) return ctx.symbol.volume_min;
        double raw = risk_amt / (pts_sl * val_pt);
        return ctx.symbol.lot_normalize(raw);
    }
};


// ============================================================
// 2. RSIStrategy
//    RSI-based mean-reversion:
//    Buy when RSI crosses above oversold_level from below.
//    Sell when RSI crosses below overbought_level from above.
// ============================================================
class RSIStrategy : public Strategy {
public:
    int    rsi_period        = 14;
    double oversold_level    = 30.0;
    double overbought_level  = 70.0;
    int    atr_period        = 14;
    double sl_atr_mult       = 1.5;
    double tp_atr_mult       = 3.0;
    double risk_pct          = 1.0;
    bool   allow_short       = true;

    int OnInit() override {
        avg_gain_ = avg_loss_ = 0.0;
        initialized_ = false;
        prev_rsi_ = 50.0;
        return INIT_SUCCEEDED;
    }

    void OnBar() override {
        if (!Trade || Trade->BarsCount() < rsi_period + 2) return;

        double cur_rsi  = _compute_rsi();
        double cur_atr  = atr(*Trade, atr_period);
        if (cur_atr <= 0.0) { prev_rsi_ = cur_rsi; return; }

        bool buy_signal  = (prev_rsi_ < oversold_level)  && (cur_rsi >= oversold_level);
        bool sell_signal = (prev_rsi_ > overbought_level) && (cur_rsi <= overbought_level);

        // Close opposing positions
        for (const auto& pos : Trade->Positions()) {
            bool is_buy = (pos.type == POSITION_TYPE_BUY);
            if (sell_signal && is_buy)  Trade->PositionClose(pos.ticket);
            if (buy_signal  && !is_buy) Trade->PositionClose(pos.ticket);
        }

        double sl_dist = cur_atr * sl_atr_mult;
        double tp_dist = cur_atr * tp_atr_mult;
        double lots    = _risk_lots(sl_dist);

        if (buy_signal && Trade->PositionsTotal() == 0) {
            Trade->Buy(lots, Trade->Ask() - sl_dist, Trade->Ask() + tp_dist, "RSI_Buy");
        } else if (sell_signal && allow_short && Trade->PositionsTotal() == 0) {
            Trade->Sell(lots, Trade->Bid() + sl_dist, Trade->Bid() - tp_dist, "RSI_Sell");
        }

        prev_rsi_ = cur_rsi;
    }

private:
    double avg_gain_, avg_loss_, prev_rsi_;
    bool   initialized_;

    double _compute_rsi() {
        int n = Trade->BarsCount();
        if (n < rsi_period + 1) return 50.0;

        if (!initialized_) {
            avg_gain_ = avg_loss_ = 0.0;
            for (int i = 0; i < rsi_period; ++i) {
                double diff = Trade->iClose(i) - Trade->iClose(i + 1);
                if (diff > 0) avg_gain_ += diff;
                else          avg_loss_ -= diff;
            }
            avg_gain_ /= rsi_period;
            avg_loss_ /= rsi_period;
            initialized_ = true;
        } else {
            double diff = Trade->iClose(0) - Trade->iClose(1);
            double gain = diff > 0 ? diff : 0.0;
            double loss = diff < 0 ? -diff : 0.0;
            avg_gain_ = (avg_gain_ * (rsi_period - 1) + gain) / rsi_period;
            avg_loss_ = (avg_loss_ * (rsi_period - 1) + loss) / rsi_period;
        }

        if (avg_loss_ < 1e-10) return 100.0;
        double rs = avg_gain_ / avg_loss_;
        return 100.0 - 100.0 / (1.0 + rs);
    }

    double _risk_lots(double sl_dist) const {
        if (sl_dist <= 1e-10) return ctx.symbol.volume_min;
        double balance  = ctx.account->balance();
        double risk_amt = balance * (risk_pct / 100.0);
        double pts_sl   = sl_dist / ctx.symbol.point;
        double val_pt   = ctx.symbol.trade_tick_value * (ctx.symbol.point / ctx.symbol.trade_tick_size);
        if (pts_sl * val_pt <= 0.0) return ctx.symbol.volume_min;
        return ctx.symbol.lot_normalize(risk_amt / (pts_sl * val_pt));
    }
};


// ============================================================
// 3. BollingerBandStrategy
//    Breakout/reversal using Bollinger Bands (20-period, 2 std dev).
//    Breakout mode: buy close above upper band, sell below lower.
//    Reversal mode: buy close below lower band, sell above upper.
// ============================================================
class BollingerBandStrategy : public Strategy {
public:
    int    period           = 20;
    double std_dev_mult     = 2.0;
    bool   reversal_mode    = true;   // true=mean-reversion, false=breakout
    int    atr_period       = 14;
    double sl_atr_mult      = 2.0;
    double tp_band_pct      = 0.5;    // TP at 50% of band width from entry
    double risk_pct         = 1.0;
    bool   allow_short      = true;

    int OnInit() override { return INIT_SUCCEEDED; }

    void OnBar() override {
        if (!Trade || Trade->BarsCount() < period + 1) return;

        // Compute Bollinger Bands
        double sma = sma_close(*Trade, period);
        double sq_sum = 0.0;
        for (int i = 0; i < period; ++i) {
            double d = Trade->iClose(i) - sma;
            sq_sum += d * d;
        }
        double sd   = std::sqrt(sq_sum / period);
        double upper = sma + std_dev_mult * sd;
        double lower = sma - std_dev_mult * sd;
        double band_width = upper - lower;

        double cur_close  = Trade->iClose(0);
        double prev_close = Trade->iClose(1);
        double cur_atr    = atr(*Trade, atr_period);
        if (cur_atr <= 0.0) return;

        bool above_upper = cur_close > upper && prev_close <= upper;
        bool below_lower = cur_close < lower && prev_close >= lower;

        for (const auto& pos : Trade->Positions()) {
            bool is_buy = (pos.type == POSITION_TYPE_BUY);
            if (reversal_mode) {
                // Close longs when price reaches SMA (mean)
                if (is_buy && cur_close >= sma)  Trade->PositionClose(pos.ticket);
                if (!is_buy && cur_close <= sma) Trade->PositionClose(pos.ticket);
            } else {
                // Close breakout longs when price crosses back inside band
                if (is_buy && cur_close < upper)  Trade->PositionClose(pos.ticket);
                if (!is_buy && cur_close > lower) Trade->PositionClose(pos.ticket);
            }
        }

        if (Trade->PositionsTotal() > 0) return;

        double sl_dist = cur_atr * sl_atr_mult;
        double tp_dist = band_width * tp_band_pct;
        double lots    = _risk_lots(sl_dist);

        if (reversal_mode) {
            if (below_lower) {
                Trade->Buy(lots, Trade->Ask() - sl_dist,
                           Trade->Ask() + tp_dist, "BB_Rev_Buy");
            } else if (above_upper && allow_short) {
                Trade->Sell(lots, Trade->Bid() + sl_dist,
                            Trade->Bid() - tp_dist, "BB_Rev_Sell");
            }
        } else {
            if (above_upper) {
                Trade->Buy(lots, Trade->Ask() - sl_dist,
                           Trade->Ask() + tp_dist, "BB_Break_Buy");
            } else if (below_lower && allow_short) {
                Trade->Sell(lots, Trade->Bid() + sl_dist,
                            Trade->Bid() - tp_dist, "BB_Break_Sell");
            }
        }
    }

private:
    double _risk_lots(double sl_dist) const {
        if (sl_dist <= 1e-10) return ctx.symbol.volume_min;
        double balance  = ctx.account->balance();
        double risk_amt = balance * (risk_pct / 100.0);
        double pts_sl   = sl_dist / ctx.symbol.point;
        double val_pt   = ctx.symbol.trade_tick_value * (ctx.symbol.point / ctx.symbol.trade_tick_size);
        if (pts_sl * val_pt <= 0.0) return ctx.symbol.volume_min;
        return ctx.symbol.lot_normalize(risk_amt / (pts_sl * val_pt));
    }
};


// ============================================================
// 4. GridStrategy
//    Fixed-interval grid: place buy stops and sell stops at
//    regular intervals around current price. Classic grid EA.
// ============================================================
class GridStrategy : public Strategy {
public:
    double grid_step_pts  = 100.0;  // Grid spacing in points
    double lot_size       = 0.01;
    int    max_orders     = 5;      // Max pending orders per side
    double tp_pts         = 150.0;  // TP per order in points

    int OnInit() override {
        grid_initialized_ = false;
        return INIT_SUCCEEDED;
    }

    void OnBar() override {
        if (!Trade || Trade->BarsCount() < 2) return;

        double cur_price = (Trade->Ask() + Trade->Bid()) * 0.5;
        double step = grid_step_pts * ctx.symbol.point;
        double tp   = tp_pts * ctx.symbol.point;
        double lots = ctx.symbol.lot_normalize(lot_size);

        if (!grid_initialized_) {
            grid_base_   = cur_price;
            grid_initialized_ = true;
        }

        // Manage orders: ensure we have `max_orders` on each side
        int buy_orders  = 0;
        int sell_orders = 0;
        for (const auto& ord : Trade->Orders()) {
            if (ord.type == ORDER_TYPE_BUY_STOP)  buy_orders++;
            if (ord.type == ORDER_TYPE_SELL_STOP) sell_orders++;
        }

        // Place missing buy stops above current price
        for (int i = 1; i <= max_orders && buy_orders < max_orders; ++i, ++buy_orders) {
            double price = ctx.symbol.normalize_price(cur_price + i * step);
            double order_tp = ctx.symbol.normalize_price(price + tp);
            Trade->BuyStop(lots, price, 0.0, order_tp, 0, "Grid_Buy");
        }

        // Place missing sell stops below current price
        for (int i = 1; i <= max_orders && sell_orders < max_orders; ++i, ++sell_orders) {
            double price = ctx.symbol.normalize_price(cur_price - i * step);
            double order_tp = ctx.symbol.normalize_price(price - tp);
            if (order_tp < ctx.symbol.point) continue;
            Trade->SellStop(lots, price, 0.0, order_tp, 0, "Grid_Sell");
        }
    }

    void OnDeinit(ENUM_DEINIT_REASON) override {
        if (!Trade) return;
        // Cancel all pending grid orders
        for (const auto& ord : Trade->Orders()) {
            Trade->OrderDelete(ord.ticket);
        }
    }

private:
    bool   grid_initialized_ = false;
    double grid_base_ = 0.0;
};

} // namespace examples
} // namespace edge
