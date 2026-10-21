/**
 * EdgeEngine - tick_sim.hpp
 * Synthetic tick generation replicating MT5 "Every Tick" mode.
 *
 * MT5 "Every Tick" mode generates 12 synthetic ticks per OHLC bar
 * following the directional wave: Open -> Low -> High -> Close (bull)
 *                                 Open -> High -> Low -> Close (bear)
 * This file exposes both the 12-point wave and an "Open Prices Only"
 * pass-through, plus a real-tick replay stub.
 *
 * NOTE: This is the legacy tick sim used by the edge_engine_core module.
 * New code should use tick_generator.hpp (which uses MqlTick / MqlRates).
 */
#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include "mql_types.hpp"
#include "types.hpp"

namespace edge {
namespace ticksim {

// ============================================================
// 12-Point Synthetic Tick Wave per OHLC Bar
// (matches MT5 "Every Tick Based on Real Ticks" synthetic mode)
// ============================================================
struct WavePoint {
    double   price;
    double   ratio;  // position within bar [0,1]
};

inline std::vector<WavePoint> generate_wave(const MqlRates& bar) {
    bool bull = (bar.close >= bar.open);
    const double O = bar.open, H = bar.high, L = bar.low, C = bar.close;
    std::vector<WavePoint> wave;
    wave.reserve(12);
    if (bull) {
        // O -> L -> H -> C
        wave.push_back({O,               0.0});
        wave.push_back({O + (L-O)*0.25,  1.0/11});
        wave.push_back({O + (L-O)*0.50,  2.0/11});
        wave.push_back({L,               3.0/11});
        wave.push_back({L + (H-L)*0.25,  4.0/11});
        wave.push_back({L + (H-L)*0.50,  5.0/11});
        wave.push_back({L + (H-L)*0.75,  6.0/11});
        wave.push_back({H,               7.0/11});
        wave.push_back({H + (C-H)*0.25,  8.0/11});
        wave.push_back({H + (C-H)*0.50,  9.0/11});
        wave.push_back({H + (C-H)*0.75, 10.0/11});
        wave.push_back({C,               1.0});
    } else {
        // O -> H -> L -> C
        wave.push_back({O,               0.0});
        wave.push_back({O + (H-O)*0.25,  1.0/11});
        wave.push_back({O + (H-O)*0.50,  2.0/11});
        wave.push_back({H,               3.0/11});
        wave.push_back({H + (L-H)*0.25,  4.0/11});
        wave.push_back({H + (L-H)*0.50,  5.0/11});
        wave.push_back({H + (L-H)*0.75,  6.0/11});
        wave.push_back({L,               7.0/11});
        wave.push_back({L + (C-L)*0.25,  8.0/11});
        wave.push_back({L + (C-L)*0.50,  9.0/11});
        wave.push_back({L + (C-L)*0.75, 10.0/11});
        wave.push_back({C,               1.0});
    }
    return wave;
}

// ============================================================
// TickIterator - iterate synthetic ticks for a bar array.
// Calls on_tick(MqlTick) for every synthetic tick.
// mode = OPEN_PRICES: one call per bar at Open
// mode = EVERY_TICK:  12 calls per bar (synthetic wave)
// mode = REAL_TICKS:  replay from tick_data array
// ============================================================
struct TickIterator {
    using TickCallback = std::function<void(const MqlTick&)>;

    void iterate(
        const MqlRates* bars,
        int32_t         n_bars,
        TickMode        mode,
        double          spread_val,
        const MqlTick*  real_ticks   = nullptr,
        int64_t         n_real_ticks = 0,
        TickCallback    on_tick      = nullptr
    ) const {
        if (!on_tick) return;

        switch (mode) {
        case TickMode::OPEN_PRICES:
            for (int32_t i = 0; i < n_bars; ++i) {
                MqlTick t{};
                t.time     = bars[i].time;
                t.time_msc = bars[i].time * 1000LL;
                t.bid      = bars[i].open;
                t.ask      = bars[i].open + spread_val;
                t.last     = bars[i].open;
                t.flags    = 0x06;  // TICK_FLAG_BID | TICK_FLAG_ASK
                on_tick(t);
            }
            break;

        case TickMode::EVERY_TICK:
            for (int32_t i = 0; i < n_bars; ++i) {
                auto wave = generate_wave(bars[i]);
                int64_t bar_start_ms = bars[i].time * 1000LL;
                double sp = spread_val;
                int64_t bar_dur_ms = 300000LL;  // assume M5 = 5 min
                for (auto& wp : wave) {
                    MqlTick t{};
                    t.time_msc = bar_start_ms + (int64_t)(wp.ratio * bar_dur_ms);
                    t.time     = t.time_msc / 1000;
                    t.bid      = wp.price;
                    t.ask      = wp.price + sp;
                    t.last     = wp.price;
                    t.flags    = 0x06;
                    on_tick(t);
                }
            }
            break;

        case TickMode::REAL_TICKS:
            if (!real_ticks || n_real_ticks <= 0) return;
            for (int64_t k = 0; k < n_real_ticks; ++k)
                on_tick(real_ticks[k]);
            break;
        }
    }
};

} // namespace ticksim
} // namespace edge
