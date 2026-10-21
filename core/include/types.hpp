/**
 * EdgeEngine - Open-Source MT5 Strategy Tester Equivalent
 * types.hpp - Core shared type definitions
 *
 * All fundamental structs, enums, and constants used across the engine.
 * Designed to be ABI-stable and pybind11-bindable.
 */
#pragma once

// mql_types.hpp provides MqlRates (=Bar), MqlTick, and all MQL5 type definitions.
// Include it first so that the Bar alias is visible everywhere types.hpp is used.
#include "mql_types.hpp"

#include <cstdint>
#include <cmath>
#include <array>
#include <string>
#include <vector>
#include <limits>

namespace edge {

// ============================================================
// Constants
// ============================================================
constexpr double EPSILON         = 1e-10;
constexpr double UNSET           = std::numeric_limits<double>::quiet_NaN();
constexpr int32_t BARS_WARMUP    = 35;   // Minimum bars before signals allowed

// ============================================================
// Tick Simulation Mode (mirrors MT5 Strategy Tester)
// ============================================================
enum class TickMode : uint8_t {
    OPEN_PRICES  = 0,  // One evaluation per bar at Open[0]
    EVERY_TICK   = 1,  // Synthetic 12-point OHLC wave per bar
    REAL_TICKS   = 2,  // Replay from actual stored broker tick data
    OHLC_M1      = 3,  // 1-minute OHLC (4 ticks per bar: Open -> H/L -> L/H -> Close)
};

// ============================================================
// Execution Latency Simulation Mode (mirrors MT5 Strategy Tester)
// ============================================================
enum class LatencyMode : uint8_t {
    NONE    = 0,  // Zero latency (instant execution)
    FIXED   = 1,  // Fixed delay in milliseconds
    RANDOM  = 2,  // Random delay between latency_min_ms and latency_max_ms
};

// ============================================================
// In-Flight Order (for execution delay and slippage queue)
// ============================================================
struct InFlightOrder {
    uint64_t        ticket = 0;
    MqlTradeRequest request{};
    int64_t         submit_time_msc = 0;
    int64_t         target_time_msc = 0;
    int32_t         submit_bar = 0;
};

// ============================================================
// Optimization Criteria & Algorithm (mirrors MT5 Strategy Tester)
// ============================================================
enum class OptimizationCriterion : uint8_t {
    CUSTOM          = 0,  // Maximize Strategy::OnTester() return value
    BALANCE_MAX     = 1,  // Maximize Ending Balance
    PROFIT_FACTOR   = 2,  // Maximize Profit Factor
    EXPECTED_PAYOFF = 3,  // Maximize Expected Payoff
    DRAWDOWN_MIN    = 4,  // Minimize Drawdown %
    SHARPE          = 5,  // Maximize Sharpe Ratio
    RECOVERY_FACTOR = 6,  // Maximize Recovery Factor
};

enum class OptimizationAlgorithm : uint8_t {
    EXHAUSTIVE = 0,  // Complete Slow Grid Search
    GENETIC    = 1,  // Fast Genetic Algorithm
};

// ============================================================
// Trade Direction
// ============================================================
enum class Direction : int8_t {
    FLAT  =  0,
    LONG  =  1,
    SHORT = -1,
};

// ============================================================
// Exit Method (matches backtest.py exit_method integers)
// ============================================================
enum class ExitMethod : int32_t {
    FIXED_RR        = 0,  // Version A: Fixed SL + TP
    BREAKEVEN       = 1,  // Version B: Breakeven at +1R
    TRAIL_2R        = 2,  // Version C: Trail after +2R
    ATR_TRAIL_NO_TP = 3,  // Version D: Pure ATR trailing, no TP
    TRAIL_ACTIVATE  = 4,  // Version E: Trail after activate_mult*R
    RATCHET         = 5,  // Version F: Ratchet trail
    CLOSE_TRAIL     = 6,  // Version G: Close-based trail
};

// ============================================================
// Entry SL Mode
// ============================================================
enum class EntrySLMode : int32_t {
    ATR_MULT   = 0,  // SL distance = ATR * sl_mult
    FIXED_PTS  = 1,  // SL distance = fixed_points * point
};

// ============================================================
// Optimization Algorithm
// ============================================================
enum class OptAlgo : uint8_t {
    EXHAUSTIVE   = 0,  // Full grid search
    RANDOM       = 1,  // Random sampling
    GENETIC      = 2,  // Genetic algorithm (differential evolution)
};


// Note: Bar is now an alias to MqlRates (defined in mql_types.hpp, included above).
// Note: Tick / MqlTick is also defined in mql_types.hpp.

// ============================================================
// Symbol Specification (mirrors MT5 SymbolInfo)
// ============================================================
struct SymbolSpec {
    std::string name;
    double  point;
    int32_t digits;
    double  trade_tick_size;
    double  trade_tick_value;
    double  spread_val;        // in price units
    int32_t spread_points;
    double  volume_min;
    double  volume_max;
    double  volume_step;
    double  commission_per_lot; // round-trip per lot (base currency)
    double  trade_stops_level;  // points
    double  trade_freeze_level; // points
    double  swap_long;          // per lot per night (currency)
    double  swap_short;
    int32_t swap_mode;          // 0=off, 1=points, 2=money

    // Convenience helpers
    double normalize_price(double price) const {
        if (point <= EPSILON) return price;
        return std::round(price / point) * point;
    }
    double points_to_price(double pts) const { return pts * point; }
    double price_to_points(double price) const {
        return (point > EPSILON) ? price / point : price;
    }
    double price_pnl(double price_diff, double lots) const {
        if (trade_tick_size > EPSILON && trade_tick_value > EPSILON)
            return (price_diff / trade_tick_size) * trade_tick_value * lots;
        return price_diff * lots;
    }
    double commission_cost(double lots) const {
        return lots * commission_per_lot * 2.0;  // round-trip
    }
    double lot_normalize(double raw_lots) const {
        if (volume_step <= EPSILON) return raw_lots;
        double stepped = std::round(raw_lots / volume_step) * volume_step;
        return std::max(volume_min, std::min(volume_max, stepped));
    }
};

// ============================================================
// Trade Record (completed trade)
// ============================================================
struct Trade {
    int64_t   ticket;
    Direction dir;
    int32_t   entry_bar;
    int32_t   exit_bar;
    int64_t   entry_time;
    int64_t   exit_time;
    double    entry_price;
    double    exit_price;
    double    lots;
    double    sl;
    double    tp;
    double    pnl;         // net after commission
    double    gross_pnl;   // before commission
    double    commission;
    double    max_favorable_excursion;   // MFE
    double    max_adverse_excursion;     // MAE
    int32_t   bars_held;
    ExitMethod exit_reason;
};

// ============================================================
// Open Position (live state during simulation)
// ============================================================
struct Position {
    Direction  dir        = Direction::FLAT;
    double     entry_price = 0.0;
    double     sl         = 0.0;
    double     tp         = 0.0;
    double     lots       = 0.0;
    int32_t    entry_bar  = 0;
    int64_t    entry_time = 0;
    int64_t    ticket     = 0;
    double     sl_distance = 0.0;
    double     trail_distance = 0.0;
    double     be_trigger_dist = 0.0;
    double     highest_high = 0.0;
    double     lowest_low  = 0.0;
    double     highest_close = 0.0;
    double     lowest_close  = 0.0;
    double     mfe         = 0.0;
    double     mae         = 0.0;

    bool is_flat() const { return dir == Direction::FLAT; }
    bool is_long() const { return dir == Direction::LONG; }
    bool is_short() const { return dir == Direction::SHORT; }
};

// ============================================================
// Backtest Result (output of a single run)
// ============================================================
struct BacktestResult {
    double net_profit;
    double net_profit_pct;
    double ending_balance;
    int32_t total_trades;
    int32_t win_count;
    int32_t loss_count;
    double  win_rate;
    double  profit_factor;
    double  sharpe;
    double  sortino;
    double  calmar;
    double  max_drawdown;       // % of equity peak
    double  max_drawdown_abs;   // in currency
    double  avg_win;
    double  avg_loss;
    double  expectancy;
    double  avg_bars_held;
    double  gross_profit;
    double  gross_loss;
    double  recovery_factor;
    double  spread_val;
    double  commission_per_lot;

    std::vector<Trade>  trades;
    std::vector<double> balance_curve;
    std::vector<double> equity_curve;
};

// ============================================================
// Optimization Parameter Range
// ============================================================
struct ParamRange {
    std::string name;
    double      start;
    double      stop;
    double      step;
    bool        is_int = false;

    int32_t count() const {
        if (step <= EPSILON) return 1;
        return static_cast<int32_t>(std::floor((stop - start) / step)) + 1;
    }
};

// ============================================================
// Strategy Parameters (key-value map wrapper)
// ============================================================
using ParamMap = std::vector<std::pair<std::string, double>>;

inline double get_param(const ParamMap& p, const std::string& key, double default_val = 0.0) {
    for (const auto& kv : p) {
        if (kv.first == key) return kv.second;
    }
    return default_val;
}

// ============================================================
// Optimization Result Row
// ============================================================
struct OptResult {
    ParamMap        params;
    BacktestResult  result;
    double          fitness;
};

} // namespace edge
