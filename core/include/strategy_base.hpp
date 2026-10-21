/**
 * EdgeEngine - Open-Source MT5 Strategy Tester Replica
 * strategy_base.hpp
 *
 * Abstract Strategy interface mirroring the MQL5 EA lifecycle:
 *   OnInit()              - Called once at start of simulation
 *   OnDeinit(reason)      - Called once at end of simulation
 *   OnTick(tick)          - Called on every synthetic or real tick
 *   OnBar(rates, idx)     - Called on every new bar close (optional, convenience)
 *   OnTradeTransaction()  - Called after each trade/order event
 *
 * TradeContext class mirrors MQL5's CTrade helper:
 *   Buy(), Sell(), BuyLimit(), SellLimit(), BuyStop(), SellStop()
 *   PositionClose(), PositionModify(), OrderModify(), OrderDelete()
 *
 * Strategies subclass Strategy and override whichever hooks they need.
 * The Tester class drives the lifecycle and injects dependencies.
 */
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cmath>
#include <stdexcept>
#include "mql_types.hpp"
#include "account.hpp"
#include "order_book.hpp"
#include "types.hpp"

namespace edge {

// ============================================================
// StrategyContext - runtime state injected into a strategy
// Provides access to account, symbol, and current bar data
// ============================================================
struct StrategyContext {
    Account*          account     = nullptr;
    OrderBook*        order_book  = nullptr;
    SymbolSpec        symbol;
    MqlTick           current_tick;
    int32_t           current_bar = 0;     // 0-indexed bar position in simulation
    int32_t           total_bars  = 0;     // Total bars in simulation
    const MqlRates*   rates       = nullptr; // Full price history up to current bar
    const double*     bid_array   = nullptr;
    const double*     ask_array   = nullptr;
    TickMode          tick_mode   = TickMode::OPEN_PRICES;
};


// ============================================================
// TradeContext - MQL5 CTrade equivalent
// Thin wrapper around OrderBook for ergonomic EA-style calls
// ============================================================
class TradeContext {
public:
    explicit TradeContext(StrategyContext& ctx) : ctx_(ctx) {}

    // --- Market Orders ---

    // Buy at current market Ask price
    bool Buy(double volume, double sl = 0.0, double tp = 0.0,
             const std::string& comment = "", uint64_t magic = 0)
    {
        MqlTradeRequest req{};
        req.action  = TRADE_ACTION_DEAL;
        req.symbol  = ctx_.symbol.name;
        req.type    = ORDER_TYPE_BUY;
        req.volume  = volume;
        req.price   = ctx_.current_tick.ask;
        req.sl      = ctx_.symbol.normalize_price(sl);
        req.tp      = ctx_.symbol.normalize_price(tp);
        req.comment = comment;
        req.magic   = magic;
        return ctx_.order_book->order_send(req, last_result_);
    }

    // Sell at current market Bid price
    bool Sell(double volume, double sl = 0.0, double tp = 0.0,
              const std::string& comment = "", uint64_t magic = 0)
    {
        MqlTradeRequest req{};
        req.action  = TRADE_ACTION_DEAL;
        req.symbol  = ctx_.symbol.name;
        req.type    = ORDER_TYPE_SELL;
        req.volume  = volume;
        req.price   = ctx_.current_tick.bid;
        req.sl      = ctx_.symbol.normalize_price(sl);
        req.tp      = ctx_.symbol.normalize_price(tp);
        req.comment = comment;
        req.magic   = magic;
        return ctx_.order_book->order_send(req, last_result_);
    }

    // --- Pending Orders ---

    bool BuyLimit(double volume, double price, double sl = 0.0, double tp = 0.0,
                  int64_t expiry = 0, const std::string& comment = "", uint64_t magic = 0)
    {
        MqlTradeRequest req{};
        req.action      = TRADE_ACTION_PENDING;
        req.symbol      = ctx_.symbol.name;
        req.type        = ORDER_TYPE_BUY_LIMIT;
        req.volume      = volume;
        req.price       = ctx_.symbol.normalize_price(price);
        req.sl          = ctx_.symbol.normalize_price(sl);
        req.tp          = ctx_.symbol.normalize_price(tp);
        req.expiration  = expiry;
        req.comment     = comment;
        req.magic       = magic;
        return ctx_.order_book->order_send(req, last_result_);
    }

    bool SellLimit(double volume, double price, double sl = 0.0, double tp = 0.0,
                   int64_t expiry = 0, const std::string& comment = "", uint64_t magic = 0)
    {
        MqlTradeRequest req{};
        req.action      = TRADE_ACTION_PENDING;
        req.symbol      = ctx_.symbol.name;
        req.type        = ORDER_TYPE_SELL_LIMIT;
        req.volume      = volume;
        req.price       = ctx_.symbol.normalize_price(price);
        req.sl          = ctx_.symbol.normalize_price(sl);
        req.tp          = ctx_.symbol.normalize_price(tp);
        req.expiration  = expiry;
        req.comment     = comment;
        req.magic       = magic;
        return ctx_.order_book->order_send(req, last_result_);
    }

    bool BuyStop(double volume, double price, double sl = 0.0, double tp = 0.0,
                 int64_t expiry = 0, const std::string& comment = "", uint64_t magic = 0)
    {
        MqlTradeRequest req{};
        req.action      = TRADE_ACTION_PENDING;
        req.symbol      = ctx_.symbol.name;
        req.type        = ORDER_TYPE_BUY_STOP;
        req.volume      = volume;
        req.price       = ctx_.symbol.normalize_price(price);
        req.sl          = ctx_.symbol.normalize_price(sl);
        req.tp          = ctx_.symbol.normalize_price(tp);
        req.expiration  = expiry;
        req.comment     = comment;
        req.magic       = magic;
        return ctx_.order_book->order_send(req, last_result_);
    }

    bool SellStop(double volume, double price, double sl = 0.0, double tp = 0.0,
                  int64_t expiry = 0, const std::string& comment = "", uint64_t magic = 0)
    {
        MqlTradeRequest req{};
        req.action      = TRADE_ACTION_PENDING;
        req.symbol      = ctx_.symbol.name;
        req.type        = ORDER_TYPE_SELL_STOP;
        req.volume      = volume;
        req.price       = ctx_.symbol.normalize_price(price);
        req.sl          = ctx_.symbol.normalize_price(sl);
        req.tp          = ctx_.symbol.normalize_price(tp);
        req.expiration  = expiry;
        req.comment     = comment;
        req.magic       = magic;
        return ctx_.order_book->order_send(req, last_result_);
    }

    // --- Position Management ---

    // Close position by ticket (full or partial)
    bool PositionClose(uint64_t ticket, double volume = 0.0) {
        return ctx_.order_book->position_close(ticket, volume);
    }

    // Modify SL/TP on an open position
    bool PositionModify(uint64_t ticket, double sl, double tp) {
        MqlTradeRequest req{};
        req.action   = TRADE_ACTION_SLTP;
        req.position = ticket;
        req.symbol   = ctx_.symbol.name;
        req.sl       = ctx_.symbol.normalize_price(sl);
        req.tp       = ctx_.symbol.normalize_price(tp);
        return ctx_.order_book->order_send(req, last_result_);
    }

    // --- Order Management ---

    bool OrderModify(uint64_t ticket, double price, double sl, double tp,
                     ENUM_ORDER_TYPE_TIME time_type = ORDER_TIME_GTC, int64_t expiry = 0)
    {
        MqlTradeRequest req{};
        req.action      = TRADE_ACTION_MODIFY;
        req.order       = ticket;
        req.symbol      = ctx_.symbol.name;
        req.price       = ctx_.symbol.normalize_price(price);
        req.sl          = ctx_.symbol.normalize_price(sl);
        req.tp          = ctx_.symbol.normalize_price(tp);
        req.type_time   = time_type;
        req.expiration  = expiry;
        return ctx_.order_book->order_send(req, last_result_);
    }

    bool OrderDelete(uint64_t ticket) {
        MqlTradeRequest req{};
        req.action = TRADE_ACTION_REMOVE;
        req.order  = ticket;
        return ctx_.order_book->order_send(req, last_result_);
    }

    // --- Result Inspection ---
    const MqlTradeResult& ResultLast() const { return last_result_; }
    bool ResultSuccess() const { return last_result_.retcode == TRADE_RETCODE_DONE || last_result_.retcode == TRADE_RETCODE_PLACED; }

    // --- Account / Positions Helpers ---
    double AccountBalance()   const { return ctx_.account->balance(); }
    double AccountEquity()    const { return ctx_.account->equity(); }
    double AccountFreeMargin() const { return ctx_.account->free_margin(); }
    double AccountMarginLevel() const { return ctx_.account->margin_level(); }
    int32_t PositionsTotal()  const { return ctx_.order_book->positions_total(); }
    int32_t OrdersTotal()     const { return ctx_.order_book->orders_total(); }
    const std::vector<PositionEntity>& Positions() const { return ctx_.order_book->active_positions(); }
    const std::vector<Order>& Orders() const { return ctx_.order_book->active_orders(); }

    // --- Pricing Helpers ---
    double Ask() const { return ctx_.current_tick.ask; }
    double Bid() const { return ctx_.current_tick.bid; }
    double Spread() const { return ctx_.current_tick.ask - ctx_.current_tick.bid; }

    // --- Historical Data Access ---
    // iOpen, iHigh, iLow, iClose in MQL5 use shift from current bar (0 = current, 1 = previous)
    double iOpen(int shift = 0) const {
        int idx = ctx_.current_bar - shift;
        if (idx < 0 || !ctx_.rates) return 0.0;
        return ctx_.rates[idx].open;
    }
    double iHigh(int shift = 0) const {
        int idx = ctx_.current_bar - shift;
        if (idx < 0 || !ctx_.rates) return 0.0;
        return ctx_.rates[idx].high;
    }
    double iLow(int shift = 0) const {
        int idx = ctx_.current_bar - shift;
        if (idx < 0 || !ctx_.rates) return 0.0;
        return ctx_.rates[idx].low;
    }
    double iClose(int shift = 0) const {
        int idx = ctx_.current_bar - shift;
        if (idx < 0 || !ctx_.rates) return 0.0;
        return ctx_.rates[idx].close;
    }
    int64_t iTime(int shift = 0) const {
        int idx = ctx_.current_bar - shift;
        if (idx < 0 || !ctx_.rates) return 0;
        return ctx_.rates[idx].time;
    }
    int64_t iVolume(int shift = 0) const {
        int idx = ctx_.current_bar - shift;
        if (idx < 0 || !ctx_.rates) return 0;
        return ctx_.rates[idx].tick_volume;
    }

    // Number of bars available to this point in the simulation
    int BarsCount() const { return ctx_.current_bar + 1; }

private:
    StrategyContext&  ctx_;
    MqlTradeResult    last_result_{};
};


// ============================================================
// INIT_SUCCEEDED / INIT_FAILED codes (matching MQL5)
// ============================================================
constexpr int INIT_SUCCEEDED = 0;
constexpr int INIT_FAILED    = 1;

// DEINIT reason codes (matching MQL5)
enum ENUM_DEINIT_REASON : int32_t {
    REASON_PROGRAM          = 0,
    REASON_REMOVE           = 1,
    REASON_RECOMPILE        = 2,
    REASON_CHARTCHANGE      = 3,
    REASON_CHARTCLOSE       = 4,
    REASON_PARAMETERS       = 5,
    REASON_ACCOUNT          = 6,
    REASON_TEMPLATE         = 7,
    REASON_INITFAILED       = 8,
    REASON_CLOSE            = 9
};


// ============================================================
// Strategy - Abstract base class
// Subclass this to implement any trading strategy.
// The Tester drives the lifecycle and provides StrategyContext.
// ============================================================
class Strategy {
public:
    virtual ~Strategy() = default;

    // Called once before the simulation begins.
    // Return INIT_SUCCEEDED (0) to allow simulation, INIT_FAILED (1) to abort.
    virtual int OnInit() { return INIT_SUCCEEDED; }

    // Called once after the simulation ends.
    virtual void OnDeinit(ENUM_DEINIT_REASON reason) {}

    // Called on every tick (EVERY_TICK mode) or bar open (OPEN_PRICES mode).
    // This is the primary entry point for strategy logic.
    virtual void OnTick() {}

    // Called after every completed bar (bar close event). Convenience hook.
    // In OPEN_PRICES mode this fires at the same time as OnTick (bar open).
    virtual void OnBar() {}

    // Called after every trade transaction (order fill, position open/close, etc.).
    virtual void OnTradeTransaction(
        const MqlTradeTransaction& trans,
        const MqlTradeRequest&     req,
        const MqlTradeResult&      res) {}

    // Called after each full bar evaluation to report indicator values.
    // Override to populate external arrays (for chart display or logging).
    virtual void OnCalculate(int bars_calculated) {}

    // Called at simulation completion during optimization to compute custom fitness.
    // Equivalent to MQL5's double OnTester(void).
    virtual double OnTester() { return 0.0; }

    // ---------------------------------------------------------
    // Injected state available to strategy implementation
    // ---------------------------------------------------------
    StrategyContext ctx;    // Set by Tester before any callbacks
    TradeContext*   Trade = nullptr; // Set by Tester before any callbacks

protected:
    // Helper shortcut: calculate a simple EMA in-strategy
    static double EMA(const double* src, int n, int period, double k) {
        if (n < period) return src[n > 0 ? n-1 : 0];
        double val = src[0];
        for (int i = 1; i < n; ++i) val = val * (1.0 - k) + src[i] * k;
        return val;
    }
};

// Shared-pointer alias for ergonomic use
using StrategyPtr = std::shared_ptr<Strategy>;

} // namespace edge
