/**
 * EdgeEngine - Open-Source MT5 Strategy Tester Replica
 * order_book.hpp - 1:1 MT5 Order Matching, Pending Order Book & Position Lifecycle Engine.
 */
#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <cmath>
#include <algorithm>
#include <functional>
#include "mql_types.hpp"
#include "account.hpp"
#include "types.hpp"

namespace edge {

class OrderBook {
public:
    using TransactionCallback = std::function<void(const MqlTradeTransaction&)>;

    explicit OrderBook(Account& account, const SymbolSpec& symbol)
        : account_(account), symbol_(symbol)
    {
        reset();
    }

    struct LatencyConfig {
        LatencyMode mode = LatencyMode::NONE;
        int32_t     fixed_delay_ms = 0;
        int32_t     min_delay_ms = 0;
        int32_t     max_delay_ms = 0;
        uint64_t    seed = 42;
    };

    void reset() {
        active_orders_.clear();
        active_positions_.clear();
        history_deals_.clear();
        history_orders_.clear();
        in_flight_orders_.clear();
        next_ticket_ = 100000;
        current_tick_ = MqlTick{};
        current_bar_index_ = 0;
    }

    void set_latency_config(const LatencyConfig& cfg) {
        latency_ = cfg;
        rng_state_ = cfg.seed != 0 ? cfg.seed : 42;
    }
    const LatencyConfig& latency_config() const { return latency_; }
    const std::vector<InFlightOrder>& in_flight_orders() const { return in_flight_orders_; }

    void set_transaction_callback(TransactionCallback cb) {
        on_transaction_ = cb;
    }

    // --- Accessors ---
    const std::vector<Order>& active_orders() const { return active_orders_; }
    const std::vector<PositionEntity>& active_positions() const { return active_positions_; }
    const std::vector<Deal>& history_deals() const { return history_deals_; }
    const std::vector<Order>& history_orders() const { return history_orders_; }

    int32_t positions_total() const { return static_cast<int32_t>(active_positions_.size()); }
    int32_t orders_total() const { return static_cast<int32_t>(active_orders_.size()); }
    int32_t history_deals_total() const { return static_cast<int32_t>(history_deals_.size()); }

    const PositionEntity* get_position_by_ticket(uint64_t ticket) const {
        for (const auto& pos : active_positions_) {
            if (pos.ticket == ticket) return &pos;
        }
        return nullptr;
    }

    const Order* get_order_by_ticket(uint64_t ticket) const {
        for (const auto& ord : active_orders_) {
            if (ord.ticket == ticket) return &ord;
        }
        return nullptr;
    }

    // --- Order Execution API (MQL5 OrderSend equivalent) ---
    bool order_send(const MqlTradeRequest& req, MqlTradeResult& result) {
        result = MqlTradeResult{};
        result.bid = current_tick_.bid;
        result.ask = current_tick_.ask;

        switch (req.action) {
            case TRADE_ACTION_DEAL: {
                if (latency_.mode != LatencyMode::NONE) {
                    int64_t delay = _compute_delay_ms();
                    if (delay > 0) {
                        InFlightOrder ifo{};
                        ifo.ticket = ++next_ticket_;
                        ifo.request = req;
                        ifo.submit_time_msc = current_tick_.time_msc;
                        ifo.target_time_msc = current_tick_.time_msc + delay;
                        ifo.submit_bar = current_bar_index_;
                        in_flight_orders_.push_back(ifo);

                        result.retcode = TRADE_RETCODE_PLACED;
                        result.order   = ifo.ticket;
                        result.comment = "Order in flight (latency simulation)";
                        return true;
                    }
                }
                return _execute_market_deal(req, result);
            }

            case TRADE_ACTION_PENDING:
                return _place_pending_order(req, result);

            case TRADE_ACTION_SLTP:
                return _modify_sltp(req, result);

            case TRADE_ACTION_MODIFY:
                return _modify_pending_order(req, result);

            case TRADE_ACTION_REMOVE:
                return _remove_pending_order(req.order, result);

            case TRADE_ACTION_CLOSE_BY:
                result.retcode = TRADE_RETCODE_REJECT;
                result.comment = "CLOSE_BY not implemented";
                return false;

            default:
                result.retcode = TRADE_RETCODE_INVALID;
                result.comment = "Unknown trade action";
                return false;
        }
    }

    // Close position directly by ticket
    bool position_close(uint64_t ticket, double volume = 0.0) {
        auto it = std::find_if(active_positions_.begin(), active_positions_.end(),
                               [ticket](const PositionEntity& p){ return p.ticket == ticket; });
        if (it == active_positions_.end()) return false;

        double close_vol = (volume <= EPSILON_PRICE || volume >= it->volume) ? it->volume : symbol_.lot_normalize(volume);
        double exit_price = it->is_buy() ? current_tick_.bid : current_tick_.ask;
        _close_position_internal(it, exit_price, close_vol, DEAL_REASON_EXPERT);
        return true;
    }

    // Update prices & evaluate intrabar triggers (Limits, Stops, SL, TP, Stopout)
    void on_price_update(const MqlTick& tick, int32_t bar_index) {
        current_tick_ = tick;
        current_bar_index_ = bar_index;

        // 0. Process in-flight orders whose latency has elapsed
        _process_in_flight_orders();

        // 1. Check Pending Orders for Execution or Expiration
        _evaluate_pending_orders(bar_index);

        // 2. Check Active Positions for SL / TP
        _evaluate_position_stops(bar_index);

        // 3. Update floating PnL and MFE / MAE
        _update_positions_pnl(bar_index);

        // 4. Update Account Equity and Margin
        account_.update_state(active_positions_, symbol_);

        // 5. Handle Stop-Out if equity / margin <= stopout level
        _handle_stopout(bar_index);
    }

    // Flush any pending in-flight orders (e.g. at end of simulation)
    void flush_in_flight_orders() {
        if (in_flight_orders_.empty()) return;
        auto orders = std::move(in_flight_orders_);
        in_flight_orders_.clear();
        for (const auto& ifo : orders) {
            MqlTradeResult res{};
            _execute_market_deal(ifo.request, res);
        }
    }

    // Close all open positions at current market prices (used at end of backtest)
    void close_all_positions(int32_t bar_index) {
        flush_in_flight_orders();
        while (!active_positions_.empty()) {
            auto it = active_positions_.begin();
            double exit_price = it->is_buy() ? current_tick_.bid : current_tick_.ask;
            _close_position_internal(it, exit_price, it->volume, DEAL_REASON_EXPERT);
        }
        account_.update_state(active_positions_, symbol_);
    }

private:
    // ----------------------------------------------------
    // Internal: Execute Instant Market Order
    // ----------------------------------------------------
    bool _execute_market_deal(const MqlTradeRequest& req, MqlTradeResult& res) {
        double vol = symbol_.lot_normalize(req.volume);
        if (vol < symbol_.volume_min || vol > symbol_.volume_max) {
            res.retcode = TRADE_RETCODE_INVALID_VOLUME;
            res.comment = "Invalid volume";
            return false;
        }

        bool is_buy = (req.type == ORDER_TYPE_BUY);
        double fill_price = is_buy ? current_tick_.ask : current_tick_.bid;
        if (fill_price <= EPSILON_PRICE) {
            res.retcode = TRADE_RETCODE_MARKET_CLOSED;
            res.comment = "Zero/invalid market price";
            return false;
        }

        // Margin check
        double req_margin = account_.calculate_required_margin(symbol_, vol, fill_price);
        if (!account_.check_margin(req_margin)) {
            res.retcode = TRADE_RETCODE_NO_MONEY;
            res.comment = "Insufficient free margin";
            return false;
        }

        // Validate SL / TP stops level if set
        double stop_dist = symbol_.trade_stops_level * symbol_.point;
        if (req.sl > EPSILON_PRICE) {
            if (is_buy && (fill_price - req.sl) < stop_dist) {
                res.retcode = TRADE_RETCODE_INVALID_STOPS;
                res.comment = "Buy SL too close to market";
                return false;
            }
            if (!is_buy && (req.sl - fill_price) < stop_dist) {
                res.retcode = TRADE_RETCODE_INVALID_STOPS;
                res.comment = "Sell SL too close to market";
                return false;
            }
        }
        if (req.tp > EPSILON_PRICE) {
            if (is_buy && (req.tp - fill_price) < stop_dist) {
                res.retcode = TRADE_RETCODE_INVALID_STOPS;
                res.comment = "Buy TP too close to market";
                return false;
            }
            if (!is_buy && (fill_price - req.tp) < stop_dist) {
                res.retcode = TRADE_RETCODE_INVALID_STOPS;
                res.comment = "Sell TP too close to market";
                return false;
            }
        }

        uint64_t ord_ticket  = ++next_ticket_;
        uint64_t deal_ticket = ++next_ticket_;

        // Record Deal
        Deal deal{};
        deal.ticket      = deal_ticket;
        deal.order       = ord_ticket;
        deal.time        = current_tick_.time;
        deal.time_msc    = current_tick_.time_msc;
        deal.type        = is_buy ? DEAL_TYPE_BUY : DEAL_TYPE_SELL;
        deal.entry       = DEAL_ENTRY_IN;
        deal.magic       = req.magic;
        deal.reason      = DEAL_REASON_EXPERT;
        deal.volume      = vol;
        deal.price       = fill_price;
        deal.commission  = symbol_.commission_cost(vol) * 0.5; // half on entry, half on exit
        deal.swap        = 0.0;
        deal.profit      = -deal.commission; // entry commission immediate charge
        deal.sl          = req.sl;
        deal.tp          = req.tp;
        deal.symbol      = req.symbol.empty() ? symbol_.name : req.symbol;
        deal.comment     = req.comment;

        // Apply entry commission to cash balance
        account_.apply_pnl(-deal.commission);

        // Update / Add Position
        if (account_.margin_mode() == ACCOUNT_MARGIN_MODE_RETAIL_HEDGING) {
            PositionEntity pos{};
            pos.ticket        = deal_ticket; // in MT5 position ID usually matches initial entry deal ticket
            pos.identifier    = deal_ticket;
            pos.time          = current_tick_.time;
            pos.time_msc      = current_tick_.time_msc;
            pos.time_update   = current_tick_.time;
            pos.type          = is_buy ? POSITION_TYPE_BUY : POSITION_TYPE_SELL;
            pos.magic         = req.magic;
            pos.volume        = vol;
            pos.price_open    = fill_price;
            pos.price_current = fill_price;
            pos.sl            = req.sl;
            pos.tp            = req.tp;
            pos.profit        = 0.0;
            pos.swap          = 0.0;
            pos.symbol        = deal.symbol;
            pos.comment       = req.comment;
            pos.highest_price = fill_price;
            pos.lowest_price  = fill_price;

            deal.position_id = pos.ticket;
            active_positions_.push_back(pos);
        } else {
            // Netting Mode: Check if position already exists for this symbol
            auto it = std::find_if(active_positions_.begin(), active_positions_.end(),
                                   [&](const PositionEntity& p){ return p.symbol == deal.symbol; });
            if (it == active_positions_.end()) {
                PositionEntity pos{};
                pos.ticket        = deal_ticket;
                pos.identifier    = deal_ticket;
                pos.time          = current_tick_.time;
                pos.time_msc      = current_tick_.time_msc;
                pos.type          = is_buy ? POSITION_TYPE_BUY : POSITION_TYPE_SELL;
                pos.magic         = req.magic;
                pos.volume        = vol;
                pos.price_open    = fill_price;
                pos.price_current = fill_price;
                pos.sl            = req.sl;
                pos.tp            = req.tp;
                pos.symbol        = deal.symbol;
                deal.position_id  = pos.ticket;
                active_positions_.push_back(pos);
            } else {
                // Adjust netting position (add volume or reverse)
                deal.position_id = it->ticket;
                if (it->type == (is_buy ? POSITION_TYPE_BUY : POSITION_TYPE_SELL)) {
                    // Adding volume: weighted average price
                    double total_vol = it->volume + vol;
                    it->price_open = (it->price_open * it->volume + fill_price * vol) / total_vol;
                    it->volume = total_vol;
                    if (req.sl > EPSILON_PRICE) it->sl = req.sl;
                    if (req.tp > EPSILON_PRICE) it->tp = req.tp;
                } else {
                    // Opposite direction: reduce or reverse
                    if (vol < it->volume) {
                        it->volume -= vol;
                    } else if (std::abs(vol - it->volume) < EPSILON_PRICE) {
                        // Fully closed
                        active_positions_.erase(it);
                    } else {
                        // Reversed
                        it->type = is_buy ? POSITION_TYPE_BUY : POSITION_TYPE_SELL;
                        it->volume = vol - it->volume;
                        it->price_open = fill_price;
                        it->sl = req.sl;
                        it->tp = req.tp;
                    }
                }
            }
        }

        history_deals_.push_back(deal);

        res.retcode = TRADE_RETCODE_DONE;
        res.deal    = deal_ticket;
        res.order   = ord_ticket;
        res.volume  = vol;
        res.price   = fill_price;
        res.comment = "Request executed";

        if (on_transaction_) {
            MqlTradeTransaction trans{};
            trans.deal        = deal_ticket;
            trans.order       = ord_ticket;
            trans.position    = deal.position_id;
            trans.symbol      = deal.symbol;
            trans.type        = TRADE_TRANSACTION_DEAL_ADD;
            trans.deal_type   = deal.type;
            trans.price       = fill_price;
            trans.volume      = vol;
            on_transaction_(trans);
        }

        return true;
    }

    // ----------------------------------------------------
    // Internal: Place Pending Order
    // ----------------------------------------------------
    bool _place_pending_order(const MqlTradeRequest& req, MqlTradeResult& res) {
        double vol = symbol_.lot_normalize(req.volume);
        if (vol < symbol_.volume_min || vol > symbol_.volume_max) {
            res.retcode = TRADE_RETCODE_INVALID_VOLUME;
            res.comment = "Invalid volume";
            return false;
        }

        uint64_t ord_ticket = ++next_ticket_;
        Order ord{};
        ord.ticket          = ord_ticket;
        ord.time_setup      = current_tick_.time;
        ord.type            = req.type;
        ord.state           = ORDER_STATE_PLACED;
        ord.time_type       = req.type_time;
        ord.time_expiration = req.expiration;
        ord.price_open      = symbol_.normalize_price(req.price);
        ord.price_stoplimit = symbol_.normalize_price(req.stoplimit);
        ord.sl              = req.sl;
        ord.tp              = req.tp;
        ord.volume_initial  = vol;
        ord.volume_current  = vol;
        ord.symbol          = req.symbol.empty() ? symbol_.name : req.symbol;
        ord.comment         = req.comment;
        ord.magic           = req.magic;

        active_orders_.push_back(ord);

        res.retcode = TRADE_RETCODE_PLACED;
        res.order   = ord_ticket;
        res.volume  = vol;
        res.price   = ord.price_open;
        res.comment = "Order placed";

        if (on_transaction_) {
            MqlTradeTransaction trans{};
            trans.order       = ord_ticket;
            trans.symbol      = ord.symbol;
            trans.type        = TRADE_TRANSACTION_ORDER_ADD;
            trans.order_type  = ord.type;
            trans.order_state = ord.state;
            trans.price       = ord.price_open;
            trans.volume      = vol;
            on_transaction_(trans);
        }

        return true;
    }

    // ----------------------------------------------------
    // Internal: Modify Position SL / TP
    // ----------------------------------------------------
    bool _modify_sltp(const MqlTradeRequest& req, MqlTradeResult& res) {
        for (auto& pos : active_positions_) {
            if (pos.ticket == req.position) {
                pos.sl = symbol_.normalize_price(req.sl);
                pos.tp = symbol_.normalize_price(req.tp);
                pos.time_update = current_tick_.time;

                res.retcode = TRADE_RETCODE_DONE;
                res.comment = "SL/TP modified";
                return true;
            }
        }
        res.retcode = TRADE_RETCODE_POSITION_CLOSED;
        res.comment = "Position not found";
        return false;
    }

    // ----------------------------------------------------
    // Internal: Modify Pending Order
    // ----------------------------------------------------
    bool _modify_pending_order(const MqlTradeRequest& req, MqlTradeResult& res) {
        for (auto& ord : active_orders_) {
            if (ord.ticket == req.order) {
                ord.price_open      = symbol_.normalize_price(req.price);
                ord.sl              = symbol_.normalize_price(req.sl);
                ord.tp              = symbol_.normalize_price(req.tp);
                ord.time_expiration = req.expiration;

                res.retcode = TRADE_RETCODE_DONE;
                res.order   = ord.ticket;
                res.comment = "Pending order modified";
                return true;
            }
        }
        res.retcode = TRADE_RETCODE_INVALID;
        res.comment = "Pending order not found";
        return false;
    }

    // ----------------------------------------------------
    // Internal: Remove Pending Order
    // ----------------------------------------------------
    bool _remove_pending_order(uint64_t ticket, MqlTradeResult& res) {
        auto it = std::find_if(active_orders_.begin(), active_orders_.end(),
                               [ticket](const Order& o){ return o.ticket == ticket; });
        if (it != active_orders_.end()) {
            Order ord = *it;
            ord.state = ORDER_STATE_CANCELED;
            history_orders_.push_back(ord);
            active_orders_.erase(it);

            res.retcode = TRADE_RETCODE_DONE;
            res.order   = ticket;
            res.comment = "Order canceled";

            if (on_transaction_) {
                MqlTradeTransaction trans{};
                trans.order       = ticket;
                trans.symbol      = ord.symbol;
                trans.type        = TRADE_TRANSACTION_ORDER_DELETE;
                trans.order_state = ORDER_STATE_CANCELED;
                on_transaction_(trans);
            }
            return true;
        }
        res.retcode = TRADE_RETCODE_INVALID;
        res.comment = "Pending order not found";
        return false;
    }

    // ----------------------------------------------------
    // Internal: Close Open Position
    // ----------------------------------------------------
    void _close_position_internal(
        std::vector<PositionEntity>::iterator it,
        double exit_price,
        double volume,
        ENUM_DEAL_REASON reason
    ) {
        bool is_buy = it->is_buy();
        double price_diff = is_buy ? (exit_price - it->price_open) : (it->price_open - exit_price);
        double gross_profit = symbol_.price_pnl(price_diff, volume);
        double exit_commission = symbol_.commission_cost(volume) * 0.5;
        double net_profit = gross_profit - exit_commission + it->swap;

        // Apply realized PnL to account cash balance
        account_.apply_pnl(net_profit);

        // Record Exit Deal
        Deal deal{};
        deal.ticket      = ++next_ticket_;
        deal.order       = ++next_ticket_;
        deal.time        = current_tick_.time;
        deal.time_msc    = current_tick_.time_msc;
        deal.type        = is_buy ? DEAL_TYPE_SELL : DEAL_TYPE_BUY;
        deal.entry       = DEAL_ENTRY_OUT;
        deal.magic       = it->magic;
        deal.reason      = reason;
        deal.position_id = it->ticket;
        deal.volume      = volume;
        deal.price       = exit_price;
        deal.commission  = exit_commission;
        deal.swap        = it->swap;
        deal.profit      = gross_profit - exit_commission;
        deal.sl          = it->sl;
        deal.tp          = it->tp;
        deal.symbol      = it->symbol;
        deal.comment     = it->comment;
        history_deals_.push_back(deal);

        if (on_transaction_) {
            MqlTradeTransaction trans{};
            trans.deal        = deal.ticket;
            trans.order       = deal.order;
            trans.position    = it->ticket;
            trans.symbol      = it->symbol;
            trans.type        = TRADE_TRANSACTION_DEAL_ADD;
            trans.deal_type   = deal.type;
            trans.price       = exit_price;
            trans.volume      = volume;
            on_transaction_(trans);
        }

        if (volume >= it->volume - EPSILON_PRICE) {
            active_positions_.erase(it);
        } else {
            it->volume -= volume;
        }
    }

    // ----------------------------------------------------
    // Internal: Evaluate Pending Orders
    // ----------------------------------------------------
    void _evaluate_pending_orders(int32_t bar_index) {
        for (auto it = active_orders_.begin(); it != active_orders_.end(); ) {
            // Expiration check
            if (it->time_expiration > 0 && current_tick_.time >= it->time_expiration) {
                it->state = ORDER_STATE_EXPIRED;
                history_orders_.push_back(*it);
                it = active_orders_.erase(it);
                continue;
            }

            bool triggered = false;
            double fill_price = 0.0;
            ENUM_ORDER_TYPE ot = it->type;

            if (ot == ORDER_TYPE_BUY_LIMIT && current_tick_.ask <= it->price_open) {
                triggered = true;
                fill_price = it->price_open; // Limit filled at limit price or better
            } else if (ot == ORDER_TYPE_SELL_LIMIT && current_tick_.bid >= it->price_open) {
                triggered = true;
                fill_price = it->price_open;
            } else if (ot == ORDER_TYPE_BUY_STOP && current_tick_.ask >= it->price_open) {
                triggered = true;
                fill_price = current_tick_.ask; // Stop filled at market (slippage possible)
            } else if (ot == ORDER_TYPE_SELL_STOP && current_tick_.bid <= it->price_open) {
                triggered = true;
                fill_price = current_tick_.bid;
            }

            if (triggered) {
                MqlTradeRequest req{};
                req.action  = TRADE_ACTION_DEAL;
                req.magic   = it->magic;
                req.symbol  = it->symbol;
                req.volume  = it->volume_current;
                req.price   = fill_price;
                req.sl      = it->sl;
                req.tp      = it->tp;
                req.type    = (ot == ORDER_TYPE_BUY_LIMIT || ot == ORDER_TYPE_BUY_STOP) ? ORDER_TYPE_BUY : ORDER_TYPE_SELL;
                req.comment = it->comment;

                MqlTradeResult res{};
                _execute_market_deal(req, res);

                it->state = ORDER_STATE_FILLED;
                history_orders_.push_back(*it);
                it = active_orders_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ----------------------------------------------------
    // Internal: Check SL and TP triggers
    // ----------------------------------------------------
    void _evaluate_position_stops(int32_t bar_index) {
        for (auto it = active_positions_.begin(); it != active_positions_.end(); ) {
            bool closed = false;
            if (it->is_buy()) {
                // Buy position exit: executed on Bid
                if (it->sl > EPSILON_PRICE && current_tick_.bid <= it->sl) {
                    // Triggered SL (simulate gap slippage if market opened/ticked below SL)
                    double exit_p = std::min(it->sl, current_tick_.bid);
                    _close_position_internal(it, exit_p, it->volume, DEAL_REASON_SL);
                    closed = true;
                } else if (it->tp > EPSILON_PRICE && current_tick_.bid >= it->tp) {
                    _close_position_internal(it, it->tp, it->volume, DEAL_REASON_TP);
                    closed = true;
                }
            } else {
                // Sell position exit: executed on Ask
                if (it->sl > EPSILON_PRICE && current_tick_.ask >= it->sl) {
                    double exit_p = std::max(it->sl, current_tick_.ask);
                    _close_position_internal(it, exit_p, it->volume, DEAL_REASON_SL);
                    closed = true;
                } else if (it->tp > EPSILON_PRICE && current_tick_.ask <= it->tp) {
                    _close_position_internal(it, it->tp, it->volume, DEAL_REASON_TP);
                    closed = true;
                }
            }

            if (!closed) {
                ++it;
            }
        }
    }

    // ----------------------------------------------------
    // Internal: Update floating PnL and MFE / MAE
    // ----------------------------------------------------
    void _update_positions_pnl(int32_t bar_index) {
        for (auto& pos : active_positions_) {
            if (pos.is_buy()) {
                pos.price_current = current_tick_.bid;
                double diff = current_tick_.bid - pos.price_open;
                pos.profit = symbol_.price_pnl(diff, pos.volume);
                pos.highest_price = std::max(pos.highest_price, current_tick_.bid);
                pos.lowest_price  = std::min(pos.lowest_price, current_tick_.bid);
                pos.mfe = std::max(pos.mfe, symbol_.price_pnl(pos.highest_price - pos.price_open, pos.volume));
                pos.mae = std::min(pos.mae, symbol_.price_pnl(pos.lowest_price - pos.price_open, pos.volume));
            } else {
                pos.price_current = current_tick_.ask;
                double diff = pos.price_open - current_tick_.ask;
                pos.profit = symbol_.price_pnl(diff, pos.volume);
                pos.lowest_price  = std::min(pos.lowest_price, current_tick_.ask);
                pos.highest_price = std::max(pos.highest_price, current_tick_.ask);
                pos.mfe = std::max(pos.mfe, symbol_.price_pnl(pos.price_open - pos.lowest_price, pos.volume));
                pos.mae = std::min(pos.mae, symbol_.price_pnl(pos.price_open - pos.highest_price, pos.volume));
            }
        }
    }

    // ----------------------------------------------------
    // Internal: Stop-Out Execution
    // ----------------------------------------------------
    void _handle_stopout(int32_t bar_index) {
        while (account_.is_stopout() && !active_positions_.empty()) {
            account_.record_stopout();

            // Find position with highest loss
            auto worst_it = active_positions_.begin();
            double min_pnl = worst_it->profit;
            for (auto it = active_positions_.begin(); it != active_positions_.end(); ++it) {
                if (it->profit < min_pnl) {
                    min_pnl = it->profit;
                    worst_it = it;
                }
            }

            double exit_price = worst_it->is_buy() ? current_tick_.bid : current_tick_.ask;
            _close_position_internal(worst_it, exit_price, worst_it->volume, DEAL_REASON_SO);
            account_.update_state(active_positions_, symbol_);
        }
    }

    // ----------------------------------------------------
    // Internal: Latency & Execution Delay Simulation
    // ----------------------------------------------------
    int64_t _compute_delay_ms() {
        if (latency_.mode == LatencyMode::FIXED) {
            return std::max(0, latency_.fixed_delay_ms);
        } else if (latency_.mode == LatencyMode::RANDOM) {
            int32_t lo = std::max(0, latency_.min_delay_ms);
            int32_t hi = std::max(lo, latency_.max_delay_ms);
            int32_t span = hi - lo + 1;
            rng_state_ = rng_state_ * 6364136223846793005ULL + 1442695040888963407ULL;
            return lo + static_cast<int32_t>((rng_state_ >> 33) % span);
        }
        return 0;
    }

    void _process_in_flight_orders() {
        if (in_flight_orders_.empty()) return;
        std::vector<InFlightOrder> remaining;
        for (auto& ifo : in_flight_orders_) {
            if (current_tick_.time_msc >= ifo.target_time_msc) {
                MqlTradeResult res{};
                _execute_market_deal(ifo.request, res);
            } else {
                remaining.push_back(ifo);
            }
        }
        in_flight_orders_ = std::move(remaining);
    }

    Account&             account_;
    SymbolSpec           symbol_;
    uint64_t             next_ticket_ = 100000;
    MqlTick              current_tick_{};
    std::vector<Order>   active_orders_;
    std::vector<PositionEntity> active_positions_;
    std::vector<Deal>    history_deals_;
    std::vector<Order>   history_orders_;
    TransactionCallback  on_transaction_;
    LatencyConfig        latency_{};
    uint64_t             rng_state_ = 42;
    std::vector<InFlightOrder> in_flight_orders_;
    int32_t              current_bar_index_ = 0;
};

} // namespace edge
