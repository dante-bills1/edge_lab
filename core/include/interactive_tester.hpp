/**
 * EdgeEngine - Open-Source MT5 Strategy Tester Replica
 * interactive_tester.hpp
 *
 * Interactive Step-by-Step and Bar-by-Bar Visual Replay Engine.
 * Replicates MetaTrader 5's Visual Tester functionality:
 *   - Step bar-by-bar (step_bar)
 *   - Step tick-by-tick (step_tick)
 *   - Live inspection of floating equity, margin, in-flight latency orders,
 *     positions, and active orders during the simulation.
 */
#pragma once

#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include "mql_types.hpp"
#include "account.hpp"
#include "order_book.hpp"
#include "strategy_base.hpp"
#include "tick_generator.hpp"
#include "types.hpp"

namespace edge {

// ============================================================
// TesterConfig - Tester-level settings
// ============================================================
struct TesterConfig {
    double    initial_balance       = 10000.0;
    int32_t   leverage              = 100;
    TickMode  tick_mode             = TickMode::OPEN_PRICES;
    ENUM_ACCOUNT_MARGIN_MODE margin_mode = ACCOUNT_MARGIN_MODE_RETAIL_HEDGING;
    double    margin_call_level     = 100.0;
    double    margin_stopout_level  = 50.0;
    bool      close_open_at_end     = true;
    bool      verbose               = false;

    // Swap and commission accounting
    bool      enable_swaps          = false;
    double    commission_per_lot    = 0.0;

    // Execution Latency & Delay Simulation (mirrors MT5)
    LatencyMode latency_mode        = LatencyMode::NONE;
    int32_t     latency_fixed_ms    = 0;
    int32_t     latency_min_ms      = 0;
    int32_t     latency_max_ms      = 0;
    uint64_t    latency_seed        = 42;
};

// ============================================================
// TesterResult - Full output of a simulation run
// ============================================================
struct TesterResult {
    // Core metrics
    double net_profit        = 0.0;
    double net_profit_pct    = 0.0;
    double gross_profit      = 0.0;
    double gross_loss        = 0.0;
    double ending_balance    = 0.0;
    double initial_balance   = 0.0;

    // Trade statistics
    int32_t total_trades     = 0;
    int32_t winning_trades   = 0;
    int32_t losing_trades    = 0;
    double  win_rate         = 0.0;
    double  profit_factor    = 0.0;
    double  expected_payoff  = 0.0;
    double  avg_profit_trade = 0.0;
    double  avg_loss_trade   = 0.0;
    double  avg_bars_held    = 0.0;
    double  max_consec_wins  = 0.0;
    double  max_consec_losses= 0.0;
    double  max_consec_profit= 0.0;
    double  max_consec_loss_amount= 0.0;

    // Equity / Drawdown
    double  max_drawdown_pct = 0.0;
    double  max_drawdown_abs = 0.0;
    double  max_balance_dd_pct = 0.0;
    double  recovery_factor  = 0.0;

    // Risk-adjusted metrics
    double  sharpe_ratio     = 0.0;
    double  sortino_ratio    = 0.0;
    double  calmar_ratio     = 0.0;

    // Optimization custom metric (from Strategy::OnTester())
    double  on_tester_value  = 0.0;

    // Account accounting
    int32_t stopout_count    = 0;
    int32_t margin_call_count= 0;
    double  total_commission = 0.0;
    double  total_swap       = 0.0;

    // Curves
    std::vector<double>  balance_curve;
    std::vector<double>  equity_curve;

    // Full deal history
    std::vector<Deal>    deals;
};

// ============================================================
// InteractiveTester
// ============================================================
class InteractiveTester {
public:
    InteractiveTester() = default;

    bool init(
        Strategy*                               strategy,
        std::vector<MqlRates>                   bars_owned,
        const SymbolSpec&                       symbol,
        const TesterConfig&                     cfg,
        std::vector<double>                     spread_series_owned = {},
        std::vector<std::vector<MqlTick>>       real_ticks_owned = {}
    ) {
        bars_storage_ = std::move(bars_owned);
        spread_storage_ = std::move(spread_series_owned);
        real_ticks_storage_ = std::move(real_ticks_owned);
        const double* ss_ptr = spread_storage_.empty() ? nullptr : spread_storage_.data();
        const auto* rt_ptr = real_ticks_storage_.empty() ? nullptr : &real_ticks_storage_;
        return init(strategy, bars_storage_.data(), static_cast<int32_t>(bars_storage_.size()),
                    symbol, cfg, ss_ptr, rt_ptr);
    }

    bool init(
        Strategy*                               strategy,
        const MqlRates*                         bars,
        int32_t                                 n,
        const SymbolSpec&                       symbol,
        const TesterConfig&                     cfg,
        const double*                           spread_series = nullptr,
        const std::vector<std::vector<MqlTick>>* real_ticks = nullptr
    ) {
        if (!strategy || !bars || n <= 0) return false;

        strategy_      = strategy;
        bars_          = bars;
        n_bars_        = n;
        cfg_           = cfg;
        spread_series_ = spread_series;

        AccountConfig acct_cfg{};
        acct_cfg.initial_balance      = cfg.initial_balance;
        acct_cfg.leverage             = cfg.leverage;
        acct_cfg.margin_mode          = cfg.margin_mode;
        acct_cfg.margin_call_level    = cfg.margin_call_level;
        acct_cfg.margin_stopout_level = cfg.margin_stopout_level;

        sym_ = symbol;
        if (cfg.commission_per_lot > 0.0)
            sym_.commission_per_lot = cfg.commission_per_lot;

        account_ = std::make_unique<Account>(acct_cfg);
        book_    = std::make_unique<OrderBook>(*account_, sym_);
        book_->set_open_prices_mode(cfg.tick_mode == TickMode::OPEN_PRICES);
        last_swap_day_ = -1;

        // Configure latency simulation
        OrderBook::LatencyConfig lat_cfg{};
        lat_cfg.mode           = cfg.latency_mode;
        lat_cfg.fixed_delay_ms = cfg.latency_fixed_ms;
        lat_cfg.min_delay_ms   = cfg.latency_min_ms;
        lat_cfg.max_delay_ms   = cfg.latency_max_ms;
        lat_cfg.seed           = cfg.latency_seed;
        book_->set_latency_config(lat_cfg);

        trade_ = std::make_unique<TradeContext>(strategy_->ctx);

        // Inject state into strategy
        strategy_->ctx.account    = account_.get();
        strategy_->ctx.order_book = book_.get();
        strategy_->ctx.symbol     = sym_;
        strategy_->ctx.total_bars = n;
        strategy_->ctx.rates      = bars;
        strategy_->ctx.tick_mode  = cfg.tick_mode;
        strategy_->Trade          = trade_.get();

        book_->set_transaction_callback(
            [this](const MqlTradeTransaction& trans) {
                MqlTradeRequest req{};
                MqlTradeResult  res{};
                strategy_->OnTradeTransaction(trans, req, res);
            }
        );

        if (strategy_->OnInit() != INIT_SUCCEEDED) {
            strategy_->OnDeinit(REASON_INITFAILED);
            is_done_ = true;
            return false;
        }

        int64_t bar_duration_ms = TickGenerator::infer_bar_duration_ms(bars, n);
        if (cfg.tick_mode == TickMode::EVERY_TICK || cfg.tick_mode == TickMode::OHLC_M1 || cfg.tick_mode == TickMode::REAL_TICKS) {
            if (real_ticks && !real_ticks->empty()) {
                tick_data_ptr_ = real_ticks;
            } else if (cfg.tick_mode == TickMode::REAL_TICKS) {
                throw std::runtime_error("TickMode::REAL_TICKS was specified, but no real tick data was provided.");
            } else {
                synth_ticks_ = TickGenerator::generate_all(
                    bars, spread_series, n, sym_.spread_val,
                    cfg.tick_mode, bar_duration_ms);
                tick_data_ptr_ = &synth_ticks_;
            }
        }

        result_ = TesterResult{};
        result_.initial_balance = cfg.initial_balance;
        result_.balance_curve.resize(n, cfg.initial_balance);
        result_.equity_curve.resize(n, cfg.initial_balance);

        current_bar_idx_     = 0;
        current_tick_in_bar_ = 0;
        is_done_             = false;
        initialized_         = true;
        return true;
    }

    // Step a single tick
    bool step_tick() {
        if (!initialized_ || is_done_ || current_bar_idx_ >= n_bars_) {
            is_done_ = true;
            return false;
        }

        strategy_->ctx.current_bar = current_bar_idx_;
        double sp = (spread_series_ && spread_series_[current_bar_idx_] >= 0.0)
                    ? spread_series_[current_bar_idx_] : sym_.spread_val;

        if (cfg_.tick_mode == TickMode::OPEN_PRICES) {
            MqlTick tick = TickGenerator::generate_open_tick(bars_[current_bar_idx_], sp);
            strategy_->ctx.current_tick = tick;
            book_->on_price_update(tick, current_bar_idx_);
            strategy_->OnTick();
            strategy_->OnBar();
            book_->evaluate_bar_extremes(bars_[current_bar_idx_], sp, current_bar_idx_);
            if (cfg_.enable_swaps) _apply_swaps();
            book_->mark_to_bar_close(bars_[current_bar_idx_], sp, current_bar_idx_);
            _record_bar_eob(sp);
            current_bar_idx_++;
            if (current_bar_idx_ >= n_bars_) is_done_ = true;
            return true;
        }

        const auto& bar_ticks = (*tick_data_ptr_)[current_bar_idx_];
        if (current_tick_in_bar_ < static_cast<int32_t>(bar_ticks.size())) {
            const auto& tick = bar_ticks[current_tick_in_bar_];
            strategy_->ctx.current_tick = tick;
            book_->on_price_update(tick, current_bar_idx_);
            strategy_->OnTick();
            current_tick_in_bar_++;

            if (current_tick_in_bar_ >= static_cast<int32_t>(bar_ticks.size())) {
                strategy_->OnBar();
                if (cfg_.enable_swaps) _apply_swaps();
                _record_bar_eob(sp);
                current_bar_idx_++;
                current_tick_in_bar_ = 0;
                if (current_bar_idx_ >= n_bars_) is_done_ = true;
            }
            return true;
        }

        current_bar_idx_++;
        current_tick_in_bar_ = 0;
        if (current_bar_idx_ >= n_bars_) is_done_ = true;
        return !is_done_;
    }

    // Step a full bar
    bool step_bar() {
        if (!initialized_ || is_done_ || current_bar_idx_ >= n_bars_) {
            is_done_ = true;
            return false;
        }

        strategy_->ctx.current_bar = current_bar_idx_;
        double sp = (spread_series_ && spread_series_[current_bar_idx_] >= 0.0)
                    ? spread_series_[current_bar_idx_] : sym_.spread_val;

        if (cfg_.tick_mode == TickMode::OPEN_PRICES) {
            MqlTick tick = TickGenerator::generate_open_tick(bars_[current_bar_idx_], sp);
            strategy_->ctx.current_tick = tick;
            book_->on_price_update(tick, current_bar_idx_);
            strategy_->OnTick();
            strategy_->OnBar();
            book_->evaluate_bar_extremes(bars_[current_bar_idx_], sp, current_bar_idx_);
            if (cfg_.enable_swaps) _apply_swaps();
            book_->mark_to_bar_close(bars_[current_bar_idx_], sp, current_bar_idx_);
            _record_bar_eob(sp);
            current_bar_idx_++;
            if (current_bar_idx_ >= n_bars_) is_done_ = true;
            return true;
        }

        const auto& bar_ticks = (*tick_data_ptr_)[current_bar_idx_];
        for (const auto& tick : bar_ticks) {
            strategy_->ctx.current_tick = tick;
            book_->on_price_update(tick, current_bar_idx_);
            strategy_->OnTick();
        }
        strategy_->OnBar();
        if (cfg_.enable_swaps) _apply_swaps();
        _record_bar_eob(sp);
        current_bar_idx_++;
        current_tick_in_bar_ = 0;
        if (current_bar_idx_ >= n_bars_) is_done_ = true;
        return true;
    }

    // Run until completion
    void run_all() {
        while (!is_done()) {
            step_bar();
        }
    }

    // Finish simulation and return result
    TesterResult finish() {
        if (!initialized_) return TesterResult{};

        if (cfg_.close_open_at_end && !book_->active_positions().empty() && n_bars_ > 0) {
            int32_t last = n_bars_ - 1;
            double sp = (spread_series_ && spread_series_[last] >= 0.0)
                        ? spread_series_[last] : sym_.spread_val;
            MqlTick final_tick{};
            final_tick.time     = bars_[last].time;
            final_tick.time_msc = bars_[last].time * 1000LL;
            final_tick.bid      = bars_[last].close;
            final_tick.ask      = bars_[last].close + sp;
            strategy_->ctx.current_tick = final_tick;
            book_->close_all_positions(last);
            account_->update_state(book_->active_positions(), sym_);
            result_.balance_curve[last] = account_->balance();
            result_.equity_curve[last]  = account_->equity();
        }

        strategy_->OnDeinit(REASON_REMOVE);
        result_.on_tester_value = strategy_->OnTester();
        result_.deals = book_->history_deals();
        compute_metrics(result_, *book_);
        is_done_ = true;
        return result_;
    }

    // Inspection API
    bool is_done() const { return is_done_; }
    int32_t current_bar() const { return current_bar_idx_; }
    int32_t total_bars() const { return n_bars_; }
    MqlTick current_tick() const { return strategy_ ? strategy_->ctx.current_tick : MqlTick{}; }
    double account_balance() const { return account_ ? account_->balance() : 0.0; }
    double account_equity() const { return account_ ? account_->equity() : 0.0; }
    double account_margin() const { return account_ ? account_->margin() : 0.0; }
    double account_free_margin() const { return account_ ? account_->free_margin() : 0.0; }
    double account_margin_level() const { return account_ ? account_->margin_level() : 0.0; }
    int32_t positions_total() const { return book_ ? book_->positions_total() : 0; }
    int32_t orders_total() const { return book_ ? book_->orders_total() : 0; }
    const std::vector<PositionEntity>& positions() const { return book_->active_positions(); }
    const std::vector<Order>& orders() const { return book_->active_orders(); }
    const std::vector<Deal>& deals() const { return book_->history_deals(); }
    const std::vector<InFlightOrder>& in_flight_orders() const { return book_->in_flight_orders(); }

    static void compute_metrics(TesterResult& r, const OrderBook& book) {
        struct TradeRec {
            double pnl;
            double gross;
            double commission;
            double swap;
            int    bars_held;
        };
        std::vector<TradeRec> trades;
        std::unordered_map<uint64_t, const Deal*> entries;
        for (const auto& d : r.deals) {
            if (d.entry == DEAL_ENTRY_IN) {
                entries[d.position_id] = &d;
            } else if (d.entry == DEAL_ENTRY_OUT) {
                double net = d.profit + d.swap;
                TradeRec rec{};
                rec.gross      = d.profit + d.commission;
                rec.commission = d.commission;
                rec.swap       = d.swap;
                rec.pnl        = net;
                rec.bars_held  = 0;
                trades.push_back(rec);
            }
        }

        int n = static_cast<int>(trades.size());
        r.total_trades = n;
        if (n == 0) {
            r.ending_balance = r.balance_curve.empty() ? r.initial_balance : r.balance_curve.back();
            r.net_profit     = r.ending_balance - r.initial_balance;
            r.net_profit_pct = r.initial_balance > 0 ? r.net_profit / r.initial_balance * 100.0 : 0.0;
            return;
        }

        r.winning_trades   = 0;
        r.losing_trades    = 0;
        r.gross_profit     = 0.0;
        r.gross_loss       = 0.0;
        r.total_commission = 0.0;
        r.total_swap       = 0.0;

        double sum_bars    = 0.0;
        int    consec_win  = 0, consec_loss = 0;
        int    max_cw = 0, max_cl = 0;
        double consec_profit = 0.0, consec_loss_amt = 0.0;
        double max_cp = 0.0, max_cla = 0.0;

        for (auto& t : trades) {
            if (t.pnl > 0.0) {
                r.winning_trades++;
                r.gross_profit += t.pnl;
                consec_win++;
                consec_profit += t.pnl;
                consec_loss = 0;
                consec_loss_amt = 0.0;
            } else {
                r.losing_trades++;
                r.gross_loss -= t.pnl;
                consec_loss++;
                consec_loss_amt -= t.pnl;
                consec_win = 0;
                consec_profit = 0.0;
            }
            max_cw  = std::max(max_cw, consec_win);
            max_cl  = std::max(max_cl, consec_loss);
            max_cp  = std::max(max_cp, consec_profit);
            max_cla = std::max(max_cla, consec_loss_amt);
            r.total_commission += t.commission;
            r.total_swap       += t.swap;
            sum_bars += t.bars_held;
        }

        r.max_consec_wins        = max_cw;
        r.max_consec_losses      = max_cl;
        r.max_consec_profit      = max_cp;
        r.max_consec_loss_amount = max_cla;

        r.win_rate         = static_cast<double>(r.winning_trades) / n;
        r.profit_factor    = r.gross_loss > 0.0 ? r.gross_profit / r.gross_loss : r.gross_profit;
        r.avg_profit_trade = r.winning_trades > 0 ? r.gross_profit / r.winning_trades : 0.0;
        r.avg_loss_trade   = r.losing_trades  > 0 ? r.gross_loss  / r.losing_trades  : 0.0;
        r.avg_bars_held    = n > 0 ? sum_bars / n : 0.0;
        r.expected_payoff  = r.win_rate * r.avg_profit_trade - (1.0 - r.win_rate) * r.avg_loss_trade;

        r.ending_balance = r.balance_curve.empty() ? r.initial_balance : r.balance_curve.back();
        r.net_profit     = r.ending_balance - r.initial_balance;
        r.net_profit_pct = r.initial_balance > 0 ? r.net_profit / r.initial_balance * 100.0 : 0.0;

        double peak_eq = r.initial_balance;
        double peak_bal = r.initial_balance;
        r.max_drawdown_abs = 0.0;
        r.max_drawdown_pct = 0.0;
        r.max_balance_dd_pct = 0.0;
        for (size_t i = 0; i < r.equity_curve.size(); ++i) {
            double eq = r.equity_curve[i];
            double bl = r.balance_curve[i];
            if (eq > peak_eq) peak_eq = eq;
            double dd = peak_eq - eq;
            r.max_drawdown_abs = std::max(r.max_drawdown_abs, dd);
            r.max_drawdown_pct = std::max(r.max_drawdown_pct, peak_eq > 0 ? dd / peak_eq * 100.0 : 0.0);
            if (bl > peak_bal) peak_bal = bl;
            double bdd = peak_bal - bl;
            r.max_balance_dd_pct = std::max(r.max_balance_dd_pct, peak_bal > 0 ? bdd / peak_bal * 100.0 : 0.0);
        }
        r.recovery_factor = r.max_drawdown_abs > 0.0 ? r.net_profit / r.max_drawdown_abs : 0.0;

        if (n > 1) {
            std::vector<double> pnls;
            pnls.reserve(trades.size());
            for (auto& t : trades) pnls.push_back(t.pnl);

            double mean = std::accumulate(pnls.begin(), pnls.end(), 0.0) / n;
            double var = 0.0, dvar = 0.0;
            for (double v : pnls) {
                var  += (v - mean) * (v - mean);
                if (v < 0.0) dvar += v * v;
            }
            double sd = std::sqrt(var / (n - 1));
            double ds = std::sqrt(dvar / n);
            double ann = std::sqrt(252.0);
            r.sharpe_ratio  = sd > 0.0 ? (mean / sd) * ann : 0.0;
            r.sortino_ratio = ds > 0.0 ? (mean / ds) * ann : 0.0;
        }
        r.calmar_ratio = r.max_drawdown_pct > 0.0 ? r.net_profit_pct / r.max_drawdown_pct : 0.0;
    }

private:
    void _apply_swaps() {
        if (!cfg_.enable_swaps || sym_.swap_mode == 0) return;
        if (!bars_ || current_bar_idx_ >= n_bars_) return;

        int64_t current_day = bars_[current_bar_idx_].time / 86400;
        if (last_swap_day_ == -1) {
            last_swap_day_ = current_day;
            return;
        }

        if (current_day > last_swap_day_) {
            // Day rollover occurred: charge swap for each calendar day passed
            for (int64_t d = last_swap_day_ + 1; d <= current_day; ++d) {
                // If swap_rollover3days is disabled (-1), it's a 24/7 continuous market (e.g. Deriv Synthetics, Crypto):
                // 1x daily swap is charged every single day including Saturday and Sunday.
                // If swap_rollover3days >= 0 (e.g. 3 for Wednesday in Forex): weekends have no swap, rollover day carries 3x.
                double multiplier = 1.0;
                if (sym_.swap_rollover3days >= 0) {
                    int dow = static_cast<int>((d + 4) % 7);
                    if (dow == 0 || dow == 6) continue; // No weekend swap for closed traditional markets
                    if (dow == sym_.swap_rollover3days) multiplier = 3.0; // Triple swap day
                }
                book_->apply_swap(multiplier);
            }
            last_swap_day_ = current_day;
        }
    }

    void _record_bar_eob(double sp) {
        account_->update_state(book_->active_positions(), sym_);
        result_.balance_curve[current_bar_idx_] = account_->balance();
        result_.equity_curve[current_bar_idx_]  = account_->equity();
    }

    Strategy*                               strategy_ = nullptr;
    const MqlRates*                         bars_ = nullptr;
    int32_t                                 n_bars_ = 0;
    SymbolSpec                              sym_{};
    TesterConfig                            cfg_{};
    const double*                           spread_series_ = nullptr;
    std::unique_ptr<Account>                account_;
    std::unique_ptr<OrderBook>              book_;
    std::unique_ptr<TradeContext>           trade_;
    std::vector<std::vector<MqlTick>>       synth_ticks_;
    const std::vector<std::vector<MqlTick>>* tick_data_ptr_ = nullptr;
    TesterResult                            result_{};
    std::vector<MqlRates>                   bars_storage_;
    std::vector<double>                     spread_storage_;
    std::vector<std::vector<MqlTick>>       real_ticks_storage_;
    int32_t                                 current_bar_idx_ = 0;
    int32_t                                 current_tick_in_bar_ = 0;
    int64_t                                 last_swap_day_ = -1;
    bool                                    is_done_ = false;
    bool                                    initialized_ = false;
};

} // namespace edge
