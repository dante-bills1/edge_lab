/**
 * EdgeEngine - Open-Source MT5 Strategy Tester Replica
 * position_manager.hpp
 *
 * High-level position lifecycle helpers that mirror MQL5's PositionGetXxx,
 * PositionSelect, trailing stop management, and the MT5 swap calculation
 * methods (SWAP_MODE_POINTS, SWAP_MODE_MONEY, SWAP_MODE_INTEREST).
 *
 * This is a stateless helper layer. The OrderBook owns the actual state.
 * Strategies call these helpers through the TradeContext or directly.
 */
#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include "mql_types.hpp"
#include "order_book.hpp"
#include "types.hpp"

namespace edge {

// ============================================================
// Swap Calculation Modes (mirrors MT5 ENUM_SYMBOL_SWAP_MODE)
// ============================================================
enum ENUM_SYMBOL_SWAP_MODE : int32_t {
    SWAP_MODE_DISABLED     = 0,  // No swap
    SWAP_MODE_POINTS       = 1,  // In points
    SWAP_MODE_CURRENCY     = 2,  // In deposit currency
    SWAP_MODE_INTEREST     = 3,  // In % annualized (daily = rate/365)
    SWAP_MODE_MARGIN_CURRENCY = 4,
    SWAP_MODE_CURRENCY_SYMBOL = 5,
    SWAP_MODE_CURRENCY_MARGIN = 6,
    SWAP_MODE_INDEX        = 14
};

// ============================================================
// TrailingStopManager
// Applies trailing stop updates to positions in the OrderBook.
// Mirrors the trailing stop logic available in MT5 EAs via
// modifying position SL after price movement conditions.
// ============================================================
class TrailingStopManager {
public:
    // Standard ATR-based trailing stop.
    // Call on every bar/tick for a position to trail its SL.
    static bool trail_atr(
        OrderBook&           book,
        const PositionEntity& pos,
        const MqlTick&        tick,
        const SymbolSpec&     sym,
        double                atr_value,
        double                atr_mult
    ) {
        if (atr_value <= 0.0 || atr_mult <= 0.0) return false;
        double trail_dist = atr_value * atr_mult;
        double freeze = std::max(sym.trade_stops_level, sym.trade_freeze_level) * sym.point;

        double new_sl = 0.0;
        if (pos.is_buy()) {
            new_sl = sym.normalize_price(tick.bid - trail_dist);
            if (new_sl <= pos.sl) return false;  // Not improving
            if ((tick.bid - new_sl) <= freeze) return false; // Inside freeze distance
        } else {
            new_sl = sym.normalize_price(tick.ask + trail_dist);
            if (new_sl >= pos.sl && pos.sl > 0.0) return false;
            if ((new_sl - tick.ask) <= freeze) return false;
        }

        MqlTradeRequest req{};
        req.action   = TRADE_ACTION_SLTP;
        req.position = pos.ticket;
        req.symbol   = pos.symbol;
        req.sl       = new_sl;
        req.tp       = pos.tp;
        MqlTradeResult res{};
        return book.order_send(req, res);
    }

    // Breakeven stop: move SL to entry price once price reaches entry + trigger_dist.
    static bool move_to_breakeven(
        OrderBook&           book,
        const PositionEntity& pos,
        const MqlTick&        tick,
        const SymbolSpec&     sym,
        double                trigger_dist,   // distance from entry to trigger BE
        double                be_offset = 0.0 // extra pips beyond entry (often 0)
    ) {
        double freeze = std::max(sym.trade_stops_level, sym.trade_freeze_level) * sym.point;

        if (pos.is_buy()) {
            if (tick.bid < pos.price_open + trigger_dist) return false; // Not triggered
            double new_sl = sym.normalize_price(pos.price_open + be_offset);
            if (new_sl <= pos.sl) return false;
            if ((tick.bid - new_sl) <= freeze) return false;
            MqlTradeRequest req{}; req.action = TRADE_ACTION_SLTP;
            req.position = pos.ticket; req.symbol = pos.symbol;
            req.sl = new_sl; req.tp = pos.tp;
            MqlTradeResult res{};
            return book.order_send(req, res);
        } else {
            if (tick.ask > pos.price_open - trigger_dist) return false;
            double new_sl = sym.normalize_price(pos.price_open - be_offset);
            if (pos.sl > 0.0 && new_sl >= pos.sl) return false;
            if ((new_sl - tick.ask) <= freeze) return false;
            MqlTradeRequest req{}; req.action = TRADE_ACTION_SLTP;
            req.position = pos.ticket; req.symbol = pos.symbol;
            req.sl = new_sl; req.tp = pos.tp;
            MqlTradeResult res{};
            return book.order_send(req, res);
        }
    }

    // Fixed-point trailing stop (non-ATR, just fixed distance).
    static bool trail_fixed(
        OrderBook&           book,
        const PositionEntity& pos,
        const MqlTick&        tick,
        const SymbolSpec&     sym,
        double                trail_points  // distance in points
    ) {
        double trail_dist = trail_points * sym.point;
        return trail_atr(book, pos, tick, sym, trail_dist, 1.0);
    }
};


// ============================================================
// SwapCalculator
// Computes daily swap charges according to MT5 swap modes.
// ============================================================
class SwapCalculator {
public:
    // Calculate swap for one night's hold for a position.
    // Returns the swap amount in account deposit currency.
    static double calculate(
        const PositionEntity& pos,
        const SymbolSpec&     sym,
        int                   swap_mode = SWAP_MODE_CURRENCY
    ) {
        double raw = pos.is_buy() ? sym.swap_long : sym.swap_short;
        if (std::abs(raw) < 1e-12) return 0.0;

        switch (swap_mode) {
            case SWAP_MODE_DISABLED:
                return 0.0;

            case SWAP_MODE_POINTS:
                // Swap in points -> convert to currency
                return raw * pos.volume * sym.trade_tick_value / sym.trade_tick_size;

            case SWAP_MODE_CURRENCY:
                // Swap already in deposit currency per lot
                return raw * pos.volume;

            case SWAP_MODE_INTEREST: {
                // Interest rate mode: raw = % annual rate
                // Daily charge = (lots * contract_size * price * rate) / (365 * 100)
                double contract_size = 100000.0;
                if (sym.trade_tick_size > 1e-12 && sym.trade_tick_value > 1e-12)
                    contract_size = sym.trade_tick_value / sym.trade_tick_size;
                double notional = pos.volume * contract_size * pos.price_open;
                return notional * raw / (365.0 * 100.0);
            }

            default:
                return raw * pos.volume;
        }
    }
};


// ============================================================
// PositionHelper - Convenience functions mirroring MQL5 position functions
// ============================================================
namespace PositionHelper {

// Select the first position matching symbol (for netting mode)
inline const PositionEntity* select(const OrderBook& book,
                                     const std::string& symbol = "")
{
    for (const auto& p : book.active_positions()) {
        if (symbol.empty() || p.symbol == symbol) return &p;
    }
    return nullptr;
}

// Select position by ticket
inline const PositionEntity* select_ticket(const OrderBook& book, uint64_t ticket) {
    return book.get_position_by_ticket(ticket);
}

// Get all positions matching magic number
inline std::vector<const PositionEntity*> select_magic(
    const OrderBook& book, uint64_t magic)
{
    std::vector<const PositionEntity*> result;
    for (const auto& p : book.active_positions()) {
        if (p.magic == magic) result.push_back(&p);
    }
    return result;
}

// Total floating profit across all positions
inline double total_profit(const OrderBook& book) {
    double total = 0.0;
    for (const auto& p : book.active_positions())
        total += p.profit;
    return total;
}

// Count buy / sell positions separately
inline int count_buys(const OrderBook& book) {
    int n = 0;
    for (const auto& p : book.active_positions())
        if (p.is_buy()) n++;
    return n;
}

inline int count_sells(const OrderBook& book) {
    int n = 0;
    for (const auto& p : book.active_positions())
        if (p.is_sell()) n++;
    return n;
}

} // namespace PositionHelper

} // namespace edge
