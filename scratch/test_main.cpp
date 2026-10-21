#include <iostream>
#include <vector>
#include "../core/include/edge_engine.hpp"

int main() {
    std::cout << "[CPP] Starting native C++ test..." << std::endl;
    int32_t n = 100;
    std::vector<edge::Bar> bars(n);
    for (int i = 0; i < n; ++i) {
        bars[i].time = 1700000000 + i * 300;
        bars[i].open = 1000.0 + i * 0.1;
        bars[i].high = 1001.0 + i * 0.1;
        bars[i].low = 999.0 + i * 0.1;
        bars[i].close = 1000.5 + i * 0.1;
        bars[i].tick_volume = 100;
        bars[i].spread = 299;
        bars[i].real_volume = 0;
    }

    edge::EngineConfig cfg;
    cfg.order.symbol.point = 0.01;
    cfg.order.symbol.trade_tick_size = 0.01;
    cfg.order.symbol.trade_tick_value = 0.01;
    cfg.order.symbol.volume_min = 0.01;
    cfg.order.symbol.volume_max = 100.0;
    cfg.order.symbol.volume_step = 0.01;
    cfg.order.symbol.spread_val = 2.99;

    edge::Engine eng;
    std::cout << "[CPP] Running backtest on 100 bars..." << std::endl;
    auto res = eng.run_backtest(bars.data(), n, nullptr, cfg);
    std::cout << "[CPP] Backtest complete!" << std::endl;
    std::cout << "Trades: " << res.total_trades << ", Profit: " << res.net_profit_pct << "%" << std::endl;
    return 0;
}
