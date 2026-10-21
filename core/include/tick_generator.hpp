/**
 * EdgeEngine - Open-Source MT5 Strategy Tester Replica
 * tick_generator.hpp
 *
 * Reproduces the MetaTrader 5 Strategy Tester tick generation engine:
 *
 *  TickMode::OPEN_PRICES  - One synthetic tick per bar at the Open price.
 *                           Classic "Open Prices Only" mode. Fastest.
 *
 *  TickMode::EVERY_TICK   - MT5's proprietary 12-point synthetic OHLC wave.
 *                           Reconstructs intrabar price movement deterministically.
 *                           Tick sequence per bar follows the canonical wave:
 *                             Open -> [pre-trend midpoints] -> H or L first
 *                             -> L or H -> Close (then next Open)
 *                           Direction (H-first vs L-first) is heuristically
 *                           inferred from Open/Close relationship.
 *
 *  TickMode::REAL_TICKS   - Pass-through: accepts externally supplied MqlTick[]
 *                           arrays. Intended for broker recorded tick replays.
 */
#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include "mql_types.hpp"
#include "types.hpp"

namespace edge {

// MT5 tick flags (mirrors MQL5 TICK_FLAG_*)
namespace TickFlag {
    constexpr uint32_t BID    = 0x02;
    constexpr uint32_t ASK    = 0x04;
    constexpr uint32_t LAST   = 0x08;
    constexpr uint32_t VOLUME = 0x10;
    constexpr uint32_t BUY    = 0x20;
    constexpr uint32_t SELL   = 0x40;
}

class TickGenerator {
public:
    /**
     * Generate synthetic ticks for a single OHLCV bar.
     * Mirrors MT5 Strategy Tester "Every Tick Based on Real Ticks" behaviour
     * when real ticks are unavailable: deterministic 12-point OHLC wave.
     *
     * @param bar       The OHLCV bar to decompose
     * @param spread    Spread in price units (applied to all generated ticks)
     * @param bar_time_ms  Bar open time in milliseconds
     * @param bar_duration_ms  Duration of bar in ms (e.g. 300000 for M5)
     * @return          Vector of ticks for the bar
     */
    static std::vector<MqlTick> generate_bar_ticks(
        const MqlRates& bar,
        double spread,
        int64_t bar_time_ms,
        int64_t bar_duration_ms = 300000  // M5 default
    ) {
        std::vector<MqlTick> ticks;
        ticks.reserve(12);

        double O = bar.open;
        double H = bar.high;
        double L = bar.low;
        double C = bar.close;

        // MT5's canonical 12-point OHLC wave.
        // The heuristic for H/L order is:
        //   - If Close >= Open (bullish bar): Low comes first (dip then rally)
        //   - If Close < Open (bearish bar):  High comes first (spike then sell-off)
        // This matches MT5 Strategy Tester documentation and reverse-engineered behavior.
        bool bull_bar = (C >= O);
        bool h_first  = !bull_bar;  // true = High before Low in intrabar sequence

        // 12-point Chebyshev-distributed price sequence
        // Open -> intermediate points -> first extreme -> second extreme -> Close
        std::vector<double> wave;
        wave.reserve(12);

        // Point 1: Open
        wave.push_back(O);

        // Points 2-4: Move toward first extreme (H or L)
        double first_extreme  = h_first ? H : L;
        double second_extreme = h_first ? L : H;

        // Intermediate steps (3 points) toward first extreme
        double step1 = O + (first_extreme - O) * 0.33;
        double step2 = O + (first_extreme - O) * 0.66;
        wave.push_back(step1);
        wave.push_back(step2);
        wave.push_back(first_extreme);  // Point 4: first extreme

        // Points 5-6: Cross to second extreme
        double cross_mid = first_extreme + (second_extreme - first_extreme) * 0.5;
        wave.push_back(cross_mid);
        wave.push_back(second_extreme);  // Point 6: second extreme

        // Points 7-10: Move toward Close from second extreme
        double to_close1 = second_extreme + (C - second_extreme) * 0.25;
        double to_close2 = second_extreme + (C - second_extreme) * 0.5;
        double to_close3 = second_extreme + (C - second_extreme) * 0.75;
        wave.push_back(to_close1);
        wave.push_back(to_close2);
        wave.push_back(to_close3);
        wave.push_back(C);  // Point 10: Close

        // Points 11-12: At Close (volume / last two ticks)
        wave.push_back(C);
        wave.push_back(C);

        // Build MqlTick structs with evenly spaced timestamps within bar
        int64_t step_ms = bar_duration_ms / static_cast<int64_t>(wave.size());
        for (size_t i = 0; i < wave.size(); ++i) {
            MqlTick tick{};
            tick.time     = (bar_time_ms + static_cast<int64_t>(i) * step_ms) / 1000;
            tick.time_msc = bar_time_ms  + static_cast<int64_t>(i) * step_ms;
            tick.bid      = wave[i];
            tick.ask      = wave[i] + spread;
            tick.last     = wave[i];
            tick.volume   = (i == wave.size() - 1) ? static_cast<uint64_t>(bar.tick_volume) : 1;
            tick.flags    = TickFlag::BID | TickFlag::ASK;
            ticks.push_back(tick);
        }

        return ticks;
    }

    /**
     * Generate 4 canonical ticks for 1 Minute OHLC mode (TickMode::OHLC_M1).
     * Follows MT5 sequence:
     *   Bull bar (Close >= Open): Open -> Low -> High -> Close
     *   Bear bar (Close < Open):  Open -> High -> Low -> Close
     */
    static std::vector<MqlTick> generate_ohlc_m1_ticks(
        const MqlRates& bar,
        double spread,
        int64_t bar_time_ms,
        int64_t bar_duration_ms = 60000
    ) {
        std::vector<MqlTick> ticks;
        ticks.reserve(4);

        double O = bar.open;
        double H = bar.high;
        double L = bar.low;
        double C = bar.close;

        bool bull_bar = (C >= O);
        double prices[4];
        prices[0] = O;
        prices[1] = bull_bar ? L : H;
        prices[2] = bull_bar ? H : L;
        prices[3] = C;

        int64_t step_ms = bar_duration_ms / 4;
        for (int i = 0; i < 4; ++i) {
            MqlTick tick{};
            tick.time     = (bar_time_ms + static_cast<int64_t>(i) * step_ms) / 1000;
            tick.time_msc = bar_time_ms  + static_cast<int64_t>(i) * step_ms;
            tick.bid      = prices[i];
            tick.ask      = prices[i] + spread;
            tick.last     = prices[i];
            tick.volume   = (i == 3) ? static_cast<uint64_t>(bar.tick_volume) : 1;
            tick.flags    = TickFlag::BID | TickFlag::ASK;
            ticks.push_back(tick);
        }
        return ticks;
    }

    /**
     * Generate a single open-price tick for a bar (OPEN_PRICES mode).
     */
    static MqlTick generate_open_tick(
        const MqlRates& bar,
        double spread
    ) {
        MqlTick tick{};
        tick.time     = bar.time;
        tick.time_msc = bar.time * 1000LL;
        tick.bid      = bar.open;
        tick.ask      = bar.open + spread;
        tick.last     = bar.open;
        tick.volume   = static_cast<uint64_t>(bar.tick_volume);
        tick.flags    = TickFlag::BID | TickFlag::ASK;
        return tick;
    }

    /**
     * Generate all ticks for the full bar array according to TickMode.
     * REAL_TICKS mode is not handled here — caller must supply real ticks directly.
     */
    static std::vector<std::vector<MqlTick>> generate_all(
        const MqlRates* bars,
        const double*   spread_series,   // per-bar spread, or nullptr (use sym.spread_val)
        int32_t         n_bars,
        double          default_spread,
        TickMode        mode,
        int64_t         bar_duration_ms = 300000  // M5 default
    ) {
        std::vector<std::vector<MqlTick>> result;
        result.reserve(n_bars);

        for (int32_t i = 0; i < n_bars; ++i) {
            double sp = (spread_series && spread_series[i] >= 0.0)
                        ? spread_series[i] : default_spread;

            if (mode == TickMode::EVERY_TICK) {
                result.push_back(generate_bar_ticks(
                    bars[i], sp, bars[i].time * 1000LL, bar_duration_ms));
            } else if (mode == TickMode::OHLC_M1) {
                result.push_back(generate_ohlc_m1_ticks(
                    bars[i], sp, bars[i].time * 1000LL, bar_duration_ms));
            } else {
                // OPEN_PRICES: single tick per bar
                result.push_back({ generate_open_tick(bars[i], sp) });
            }
        }
        return result;
    }

    /**
     * Infer bar duration in ms from bar timestamps.
     * Falls back to M5 (300,000 ms) if insufficient data.
     */
    static int64_t infer_bar_duration_ms(const MqlRates* bars, int32_t n) {
        if (n < 2) return 300000LL;
        // Take median of first 10 diffs to be robust to gaps
        std::vector<int64_t> diffs;
        int32_t check = std::min(n - 1, 10);
        for (int32_t i = 0; i < check; ++i) {
            int64_t diff = (bars[i+1].time - bars[i].time) * 1000LL;
            if (diff > 0) diffs.push_back(diff);
        }
        if (diffs.empty()) return 300000LL;
        std::sort(diffs.begin(), diffs.end());
        return diffs[diffs.size() / 2];
    }
};

} // namespace edge
