"""
edge_engine/tests/test_advanced_features.py

Comprehensive tests for:
1. Latency & Delay Queue (Slippage Simulation)
2. Complete MT5 Tick Modes (OHLC_M1 4-tick intrabar wave)
3. InteractiveTester Bar-by-Bar and Tick-by-Tick visual replay
4. Custom Optimization Metric via Strategy::OnTester()
"""
import pytest
import numpy as np
import pandas as pd
import edge_tester as et

def _generate_test_bars(n: int = 100):
    np.random.seed(42)
    t0 = 1700000000
    times = np.array([t0 + i * 300 for i in range(n)], dtype=np.int64)
    price = 1000.0
    opens, highs, lows, closes = [], [], [], []
    for _ in range(n):
        o = price
        delta = np.random.randn() * 2.0
        c = o + delta
        h = max(o, c) + abs(np.random.randn())
        l = min(o, c) - abs(np.random.randn())
        opens.append(o)
        highs.append(h)
        lows.append(l)
        closes.append(c)
        price = c
    return {
        "open": np.array(opens, dtype=np.float64),
        "high": np.array(highs, dtype=np.float64),
        "low": np.array(lows, dtype=np.float64),
        "close": np.array(closes, dtype=np.float64),
        "time": times,
        "volume": np.full(n, 100, dtype=np.int64),
        "spread": np.full(n, 10, dtype=np.int32),
    }

def _make_spec():
    s = et.SymbolSpec()
    s.name = "TestSymbol"
    s.point = 0.01
    s.digits = 2
    s.trade_tick_size = 0.01
    s.trade_tick_value = 0.01
    s.spread_val = 0.10
    s.volume_min = 0.01
    s.volume_max = 100.0
    s.volume_step = 0.01
    s.commission_per_lot = 0.0
    return s


class SimpleBuyerStrategy(et.Strategy):
    def on_init(self):
        self.entered = False
        return 0

    def on_bar(self):
        if not self.entered and self.trade.positions_total() == 0:
            self.trade.buy(1.0, 0.0, 0.0)
            self.entered = True

    def on_tester(self):
        # Custom optimization metric: return ending balance minus initial
        return self.trade.account_balance() - 10000.0


class TestAdvancedMT5Features:
    def test_ohlc_m1_tick_mode(self):
        bars = _generate_test_bars(50)
        spec = _make_spec()
        cfg = et.TesterConfig()
        cfg.tick_mode = et.TickMode.OHLC_M1

        tester = et.Tester()
        strat = SimpleBuyerStrategy()
        res = tester.run(
            strat,
            bars["open"], bars["high"], bars["low"], bars["close"],
            bars["time"], bars["volume"], bars["spread"],
            spec, cfg
        )
        assert res.total_trades > 0
        assert len(res.deals) >= 2
        assert len(res.balance_curve) == 50

    def test_latency_delay_queue(self):
        bars = _generate_test_bars(50)
        spec = _make_spec()
        cfg = et.TesterConfig()
        cfg.tick_mode = et.TickMode.EVERY_TICK
        cfg.latency_mode = et.LatencyMode.FIXED
        cfg.latency_fixed_ms = 500  # 500ms delay

        tester = et.Tester()
        strat = SimpleBuyerStrategy()
        res = tester.run(
            strat,
            bars["open"], bars["high"], bars["low"], bars["close"],
            bars["time"], bars["volume"], bars["spread"],
            spec, cfg
        )
        assert res.total_trades > 0
        assert len(res.deals) >= 2

    def test_interactive_tester_replay(self):
        bars = _generate_test_bars(30)
        spec = _make_spec()
        cfg = et.TesterConfig()
        cfg.tick_mode = et.TickMode.OPEN_PRICES

        replay = et.InteractiveTester()
        strat = SimpleBuyerStrategy()
        ok = replay.init(
            strat,
            bars["open"], bars["high"], bars["low"], bars["close"],
            bars["time"], bars["volume"], bars["spread"],
            spec
        )
        assert ok is True
        assert replay.current_bar() == 0
        assert replay.total_bars() == 30
        assert replay.is_done() is False

        # Step 5 bars
        for b in range(5):
            replay.step_bar()
        assert replay.current_bar() == 5
        assert replay.account_balance() > 0.0

        # Run remaining
        replay.run_all()
        assert replay.is_done() is True
        res = replay.finish()
        assert res.total_trades > 0
        assert res.on_tester_value != 0.0

    def test_on_tester_custom_fitness(self):
        bars = _generate_test_bars(40)
        spec = _make_spec()
        cfg = et.TesterConfig()

        tester = et.Tester()
        strat = SimpleBuyerStrategy()
        res = tester.run(
            strat,
            bars["open"], bars["high"], bars["low"], bars["close"],
            bars["time"], bars["volume"], bars["spread"],
            spec, cfg
        )
        assert "on_tester_value" in res.to_dict()
        assert res.on_tester_value == pytest.approx(res.net_profit, rel=1e-3)

    def test_strategy_optimizer_grid(self):
        from edge_engine.optimizer import StrategyOptimizer

        bars = _generate_test_bars(40)
        df = pd.DataFrame(bars)
        spec = _make_spec()
        opt = StrategyOptimizer(
            strategy_cls=SimpleBuyerStrategy,
            symbol_spec=spec,
            criterion=et.OptimizationCriterion.CUSTOM,
            algorithm=et.OptimizationAlgorithm.EXHAUSTIVE,
            max_workers=1,
        )
        param_grid = {"dummy_param": [1, 2, 3]}
        results = opt.optimize(df, param_grid)
        assert len(results) == 3
        assert "fitness" in results[0]

    def test_strategy_optimizer_genetic(self):
        from edge_engine.optimizer import StrategyOptimizer

        bars = _generate_test_bars(40)
        df = pd.DataFrame(bars)
        spec = _make_spec()
        opt = StrategyOptimizer(
            strategy_cls=SimpleBuyerStrategy,
            symbol_spec=spec,
            criterion=et.OptimizationCriterion.BALANCE_MAX,
            algorithm=et.OptimizationAlgorithm.GENETIC,
            max_workers=1,
        )
        param_grid = {"dummy_param": [10, 20, 30, 40, 50]}
        results = opt.optimize(df, param_grid, generations=2, population_size=4)
        assert len(results) > 0
        assert "fitness" in results[0]

    def test_latency_discarded_in_open_prices(self):
        bars = _generate_test_bars(30)
        spec = _make_spec()
        cfg = et.TesterConfig()
        cfg.tick_mode = et.TickMode.OPEN_PRICES
        cfg.latency_mode = et.LatencyMode.FIXED
        cfg.latency_fixed_ms = 5000  # Latency configured

        tester = et.Tester()
        strat = SimpleBuyerStrategy()
        res = tester.run(
            strat,
            bars["open"], bars["high"], bars["low"], bars["close"],
            bars["time"], bars["volume"], bars["spread"],
            spec, cfg
        )
        # Latency should be discarded in open prices mode, trade executes immediately on bar 0
        assert res.total_trades > 0
        assert len(res.deals) >= 2

    def test_swap_rollover_schedule(self):
        # Generate bars spanning multiple days (every bar is 4 hours = 14400s)
        n = 42 # 7 days
        t0 = 1700006400 # 2023-11-15 00:00:00 (Wednesday!)
        times = np.array([t0 + i * 14400 for i in range(n)], dtype=np.int64)
        opens = np.full(n, 1000.0, dtype=np.float64)
        highs = np.full(n, 1005.0, dtype=np.float64)
        lows = np.full(n, 995.0, dtype=np.float64)
        closes = np.full(n, 1000.0, dtype=np.float64)
        vols = np.full(n, 100, dtype=np.int64)
        spreads = np.full(n, 10, dtype=np.int32)

        spec = _make_spec()
        spec.swap_mode = 2 # currency mode
        spec.swap_long = -10.0 # -$10/night
        spec.swap_short = 5.0

        cfg = et.TesterConfig()
        cfg.enable_swaps = True
        cfg.tick_mode = et.TickMode.OPEN_PRICES

        tester = et.Tester()
        strat = SimpleBuyerStrategy()
        res = tester.run(strat, opens, highs, lows, closes, times, vols, spreads, spec, cfg)
        assert res.total_trades > 0
        # Position was held across days, should accumulate negative swap
        deals = res.deals
        exit_deal = deals[-1]
        assert exit_deal.swap < 0.0, f"Expected negative swap, got {exit_deal.swap}"

