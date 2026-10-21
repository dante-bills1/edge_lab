/**
 * EdgeEngine - Open-Source MT5 Strategy Tester Replica
 * tester.hpp
 *
 * The main simulation orchestrator - general-purpose MT5 Strategy Tester
 * equivalent. Accepts ANY Strategy subclass and runs a full event-driven
 * simulation over OHLCV bar data.
 *
 * Architecture mirrors the MT5 Strategy Tester processing loop:
 *   1. Initialize Account, Symbol, OrderBook, TradeContext
 *   2. Call strategy->OnInit()
 *   3. For each bar:
 *      a. Generate ticks (TickMode: OPEN_PRICES or EVERY_TICK)
 *      b. For each tick in the bar:
 *         - Update OrderBook prices (evaluates pending orders, SL/TP triggers)
 *         - Call strategy->OnTick()
 *      c. At bar close: call strategy->OnBar() (if OPEN_PRICES, merged with OnTick)
 *   4. Close all positions at end of data
 *   5. Call strategy->OnDeinit()
 *   6. Build and return TesterResult (trade history, metrics, curves)
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
#include "interactive_tester.hpp"

namespace edge {

// ============================================================
// Tester - General-purpose MT5 Simulation Engine
// ============================================================
class Tester {
public:
    /**
     * Run a full simulation for the given Strategy over the supplied bar data.
     *
     * @param strategy      Any Strategy subclass (OnInit/OnTick/OnBar/OnDeinit).
     *                      Must not be nullptr.
     * @param bars          Full OHLCV array from broker data (chronological order).
     * @param n             Number of bars.
     * @param symbol        Symbol specification (tick size, value, spread, etc.)
     * @param cfg           Tester-level configuration.
     * @param spread_series Optional per-bar spread override (length n), or nullptr.
     * @param real_ticks    Optional pre-loaded real tick data (for REAL_TICKS mode).
     *
     * @return TesterResult populated with all trade metrics and curves.
     */
    TesterResult run(
        Strategy*             strategy,
        const MqlRates*       bars,
        int32_t               n,
        const SymbolSpec&     symbol,
        const TesterConfig&   cfg,
        const double*         spread_series = nullptr,
        const std::vector<std::vector<MqlTick>>* real_ticks = nullptr
    ) const
    {
        if (!strategy || !bars || n <= 0)
            return TesterResult{};

        InteractiveTester interactive;
        if (!interactive.init(strategy, bars, n, symbol, cfg, spread_series, real_ticks)) {
            return TesterResult{};
        }
        interactive.run_all();
        return interactive.finish();
    }
};

} // namespace edge
