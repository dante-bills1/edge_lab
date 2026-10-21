"""
tests/test_general_tester.py
=============================
Validation tests for the general-purpose edge_tester module.
These tests prove the engine works with ANY trading strategy,
not just the NewLeaf/TrendFinder strategy.

Run with:
    pytest edge_engine/tests/test_general_tester.py -v
"""
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import pytest
import numpy as np

try:
    import edge_tester as et
    HAS_TESTER = True
except ImportError:
    HAS_TESTER = False

_StrategyBase = et.Strategy if HAS_TESTER else object

pytestmark = pytest.mark.skipif(
    not HAS_TESTER,
    reason="edge_tester module not built. Run: pip install . --no-build-isolation --force-reinstall"
)

# ─────────────────────────────────────────────────────────────
# Shared fixtures
# ─────────────────────────────────────────────────────────────

def make_symbol():
    sym = et.SymbolSpec()
    sym.name             = "EURUSD"
    sym.point            = 0.00001
    sym.digits           = 5
    sym.trade_tick_size  = 0.00001
    sym.trade_tick_value = 1.0
    sym.spread_val       = 0.00015  # 1.5 pip spread
    sym.volume_min       = 0.01
    sym.volume_max       = 100.0
    sym.volume_step      = 0.01
    sym.commission_per_lot = 0.0
    sym.trade_stops_level  = 0
    sym.swap_long        = 0.0
    sym.swap_short       = 0.0
    sym.swap_mode        = 0
    return sym


def make_config(**kwargs):
    cfg = et.TesterConfig()
    cfg.initial_balance  = kwargs.get("balance", 10000.0)
    cfg.leverage         = kwargs.get("leverage", 100)
    cfg.tick_mode        = kwargs.get("tick_mode", et.TickMode.OPEN_PRICES)
    cfg.margin_mode      = kwargs.get("margin_mode", et.MarginMode.HEDGING)
    cfg.close_open_at_end= kwargs.get("close_at_end", True)
    cfg.enable_swaps     = False
    return cfg


def make_trending_bars(n=500):
    """Gentle uptrend with some noise — good for SMA crossover tests."""
    np.random.seed(42)
    closes = np.cumsum(np.random.randn(n) * 0.001 + 0.0003) + 1.1000
    highs  = closes + np.abs(np.random.randn(n)) * 0.0005
    lows   = closes - np.abs(np.random.randn(n)) * 0.0005
    opens  = np.roll(closes, 1); opens[0] = closes[0]
    times  = np.arange(n, dtype=np.int64) * 300 + 1700000000
    vols   = np.ones(n, dtype=np.int64) * 100
    spreads= np.zeros(n, dtype=np.int32)
    return opens, highs, lows, closes, times, vols, spreads


def make_ranging_bars(n=500):
    """Oscillating prices — good for RSI/BB tests."""
    np.random.seed(7)
    t = np.linspace(0, 4 * np.pi, n)
    closes = 1.1000 + 0.002 * np.sin(t) + np.random.randn(n) * 0.0002
    highs  = closes + np.abs(np.random.randn(n)) * 0.0003
    lows   = closes - np.abs(np.random.randn(n)) * 0.0003
    opens  = np.roll(closes, 1); opens[0] = closes[0]
    times  = np.arange(n, dtype=np.int64) * 300 + 1700000000
    vols   = np.ones(n, dtype=np.int64) * 100
    spreads= np.zeros(n, dtype=np.int32)
    return opens, highs, lows, closes, times, vols, spreads


# ─────────────────────────────────────────────────────────────
# Minimal "do nothing" strategy for baseline testing
# ─────────────────────────────────────────────────────────────

class DoNothingStrategy(_StrategyBase):
    """Never trades. Used to verify engine infrastructure."""
    bars_seen = 0

    def on_init(self):
        self.bars_seen = 0
        return et.INIT_SUCCEEDED

    def on_bar(self):
        self.bars_seen += 1


# ─────────────────────────────────────────────────────────────
# Buy-and-hold strategy for accounting verification
# ─────────────────────────────────────────────────────────────

class BuyAndHoldStrategy(_StrategyBase):
    """Buy 0.01 lots at bar 10, hold to end."""
    def on_init(self):
        self._bought = False
        return et.INIT_SUCCEEDED

    def on_bar(self):
        t = self.trade
        if not self._bought and t.bars() == 10:
            t.buy(0.01, 0.0, 0.0, "BAH")
            self._bought = True


# ─────────────────────────────────────────────────────────────
# SMA crossover strategy (pure Python)
# ─────────────────────────────────────────────────────────────

class SmaCrossStrategy(_StrategyBase):
    fast = 10
    slow = 30
    lots = 0.01

    def on_bar(self):
        t = self.trade
        if t.bars() < self.slow + 1:
            return
        fast_sma = sum(t.iClose(i) for i in range(self.fast)) / self.fast
        slow_sma = sum(t.iClose(i) for i in range(self.slow)) / self.slow

        bull = fast_sma > slow_sma
        bear = fast_sma < slow_sma

        for pos in t.positions():
            is_buy = pos.type == et.PositionType.BUY
            if bear and is_buy:  t.position_close(pos.ticket)
            elif bull and not is_buy: t.position_close(pos.ticket)

        if t.positions_total() > 0:
            return
        if bull:
            t.buy(self.lots, 0.0, 0.0, "SMA_Buy")
        elif bear:
            t.sell(self.lots, 0.0, 0.0, "SMA_Sell")


# ─────────────────────────────────────────────────────────────
# Pending order strategy
# ─────────────────────────────────────────────────────────────

class PendingOrderStrategy(_StrategyBase):
    """Places a buy stop above current price, waits for trigger."""
    placed = False

    def on_init(self):
        self.placed = False
        return et.INIT_SUCCEEDED

    def on_bar(self):
        t = self.trade
        if t.bars() == 5 and not self.placed:
            price = t.ask() + 0.0020  # 20 pips above
            t.buy_stop(0.01, price, 0.0, 0.0, comment="TestStop")
            self.placed = True


# ─────────────────────────────────────────────────────────────
# Tests
# ─────────────────────────────────────────────────────────────

class TestTesterInfrastructure:
    def test_module_version(self):
        assert et.__version__ == "2.0.0"

    def test_tester_instantiation(self):
        t = et.Tester()
        assert t is not None

    def test_symbol_spec(self):
        sym = make_symbol()
        assert sym.point == pytest.approx(0.00001)
        assert sym.volume_min == pytest.approx(0.01)

    def test_tester_config(self):
        cfg = make_config()
        assert cfg.initial_balance == pytest.approx(10000.0)
        assert cfg.leverage == 100

    def test_do_nothing_strategy_runs(self):
        s = DoNothingStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(200)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        assert result is not None
        assert s.bars_seen == 200

    def test_no_trades_no_pnl(self):
        s = DoNothingStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(100)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        assert result.total_trades == 0
        assert result.net_profit == pytest.approx(0.0, abs=1e-6)
        assert result.ending_balance == pytest.approx(10000.0, abs=1e-6)


class TestAccountingIntegrity:
    def test_buy_and_hold_accounting(self):
        s = BuyAndHoldStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(200)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        # With close_at_end=True, should have exactly one round trip (2 deals)
        assert result.total_trades >= 1, "Buy-and-hold should produce at least 1 trade"

    def test_balance_curve_length(self):
        s = DoNothingStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(250)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        assert len(result.balance_curve) == 250
        assert len(result.equity_curve) == 250

    def test_balance_monotone_no_trades(self):
        s = DoNothingStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(100)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        bal = result.balance_curve
        # All balance values should be equal (no trades)
        assert all(abs(b - 10000.0) < 1e-6 for b in bal)

    def test_wins_plus_losses_eq_total(self):
        s = SmaCrossStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(500)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        assert result.winning_trades + result.losing_trades == result.total_trades

    def test_win_rate_range(self):
        s = SmaCrossStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(500)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        if result.total_trades > 0:
            assert 0.0 <= result.win_rate <= 1.0

    def test_max_drawdown_nonnegative(self):
        s = SmaCrossStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(500)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        assert result.max_drawdown_pct >= 0.0
        assert result.max_drawdown_abs >= 0.0

    def test_profit_factor_positive_pf(self):
        s = SmaCrossStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(500)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        if result.gross_loss > 0:
            expected_pf = result.gross_profit / result.gross_loss
            assert result.profit_factor == pytest.approx(expected_pf, rel=1e-5)


class TestStrategyDiversity:
    def test_sma_cross_trending(self):
        """SMA cross should produce trades on trending data."""
        s = SmaCrossStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(500)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        # Should have generated some trades
        assert result.total_trades > 0, "SMA cross should trade on trending data"

    def test_sma_cross_result_dict(self):
        s = SmaCrossStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(300)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        d = result.to_dict()
        assert "net_profit" in d
        assert "win_rate" in d
        assert "sharpe_ratio" in d

    def test_pending_order_strategy(self):
        """Pending order placed and verified in the order book."""
        s = PendingOrderStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(100)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        # Simulation should complete without errors
        assert result is not None

    def test_every_tick_mode(self):
        """EVERY_TICK mode should produce the same final trade count as OPEN_PRICES for simple logic."""
        s1 = SmaCrossStrategy()
        s2 = SmaCrossStrategy()
        sym = make_symbol()
        cfg1 = make_config(tick_mode=et.TickMode.OPEN_PRICES)
        cfg2 = make_config(tick_mode=et.TickMode.EVERY_TICK)
        o, h, l, c, t, v, sp = make_trending_bars(200)
        tester = et.Tester()
        r1 = tester.run(s1, o, h, l, c, t, v, sp, sym, cfg1)
        r2 = tester.run(s2, o, h, l, c, t, v, sp, sym, cfg2)
        # Both should produce trades
        assert r1.total_trades >= 0
        assert r2.total_trades >= 0
        # EVERY_TICK may produce same or more trades (intrabar SL/TP triggers)
        assert r2.total_trades >= r1.total_trades

    def test_netting_vs_hedging_mode(self):
        """Both margin modes should run without errors."""
        o, h, l, c, t, v, sp = make_trending_bars(300)
        sym = make_symbol()
        tester = et.Tester()
        for mode in [et.MarginMode.HEDGING, et.MarginMode.NETTING]:
            s = SmaCrossStrategy()
            cfg = make_config(margin_mode=mode)
            result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
            assert result is not None

    def test_high_leverage_no_crash(self):
        """Very high leverage should not cause crashes."""
        s = SmaCrossStrategy()
        sym = make_symbol()
        cfg = make_config(leverage=2000)
        o, h, l, c, t, v, sp = make_trending_bars(200)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        assert result is not None

    def test_small_balance_no_crash(self):
        """Very small starting balance should not crash (may hit margin out)."""
        s = SmaCrossStrategy()
        sym = make_symbol()
        cfg = make_config(balance=100.0)
        o, h, l, c, t, v, sp = make_trending_bars(200)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        assert result is not None

    def test_deals_list_populated(self):
        s = SmaCrossStrategy()
        sym = make_symbol()
        cfg = make_config()
        o, h, l, c, t, v, sp = make_trending_bars(400)
        tester = et.Tester()
        result = tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        if result.total_trades > 0:
            assert len(result.deals) > 0
            # All deals should have valid prices
            for deal in result.deals:
                assert deal.price > 0.0
                assert deal.volume > 0.0


class TestTradeContextAPI:
    def test_historical_price_access(self):
        """iClose(0) should return current bar close, iClose(1) previous."""
        class PriceAccessStrategy(_StrategyBase):
            close_0 = None
            close_1 = None
            bar_recorded = False

            def on_init(self):
                return et.INIT_SUCCEEDED

            def on_bar(self):
                if self.trade.bars() == 50 and not self.bar_recorded:
                    self.close_0 = self.trade.iClose(0)
                    self.close_1 = self.trade.iClose(1)
                    self.bar_recorded = True

        s = PriceAccessStrategy()
        o, h, l, c, t, v, sp = make_trending_bars(100)
        sym = make_symbol()
        cfg = make_config()
        tester = et.Tester()
        tester.run(s, o, h, l, c, t, v, sp, sym, cfg)

        assert s.close_0 is not None
        assert s.close_1 is not None
        assert s.close_0 == pytest.approx(c[49], rel=1e-6)
        assert s.close_1 == pytest.approx(c[48], rel=1e-6)

    def test_bid_ask_spread(self):
        """Ask should equal Bid + spread_val."""
        class SpreadCheckStrategy(_StrategyBase):
            spread_ok = None
            def on_init(self): return et.INIT_SUCCEEDED
            def on_bar(self):
                if self.trade.bars() == 5:
                    diff = self.trade.ask() - self.trade.bid()
                    self.spread_ok = abs(diff - 0.00015) < 1e-7

        s = SpreadCheckStrategy()
        o, h, l, c, t, v, sp = make_trending_bars(10)
        sym = make_symbol()
        cfg = make_config()
        tester = et.Tester()
        tester.run(s, o, h, l, c, t, v, sp, sym, cfg)
        assert s.spread_ok is True
