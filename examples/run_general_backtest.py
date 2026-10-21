"""
EdgeEngine General-Purpose Tester - Python Usage Example
=========================================================
This script shows how to define and backtest custom strategies
in pure Python using the edge_tester module.

Strategies tested:
  1. SmaCrossStrategy   - EMA crossover
  2. RSIStrategy        - RSI mean-reversion
  3. BollingerBandStrategy - BB reversal

Run with:
  python edge_engine/examples/run_general_backtest.py
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import numpy as np
import pandas as pd

try:
    import edge_tester as et
except ImportError:
    print("ERROR: edge_tester module not built. Run:")
    print("  pip install . --no-build-isolation --no-deps --force-reinstall")
    sys.exit(1)

# ─────────────────────────────────────────────────────────────
# Load data
# ─────────────────────────────────────────────────────────────
DATA_PATH = os.path.join(
    os.path.dirname(__file__), "..", "..",
    "backtest_suite", "historical_data_cache",
    "jump_75_index_100k_m5.parquet"
)

df = pd.read_parquet(DATA_PATH)
print(f"Loaded {len(df):,} bars from {df.index[0]} to {df.index[-1]}")

open_  = df["open"].values.astype(np.float64)
high   = df["high"].values.astype(np.float64)
low    = df["low"].values.astype(np.float64)
close  = df["close"].values.astype(np.float64)
time   = df.index.astype(np.int64) // 10**9
vol    = df.get("tick_volume", pd.Series(np.ones(len(df), dtype=np.int64))).values.astype(np.int64)
spread = np.zeros(len(df), dtype=np.int32)

# ─────────────────────────────────────────────────────────────
# Symbol Specification (Volatility 75 Index)
# ─────────────────────────────────────────────────────────────
sym = et.SymbolSpec()
sym.name              = "Volatility 75 Index"
sym.point             = 0.01
sym.digits            = 2
sym.trade_tick_size   = 0.01
sym.trade_tick_value  = 0.10      # $0.10 per tick per standard lot
sym.spread_val        = 0.80      # ~80 point synthetic spread
sym.volume_min        = 0.001
sym.volume_max        = 50.0
sym.volume_step       = 0.001
sym.commission_per_lot= 0.0
sym.trade_stops_level = 0
sym.swap_long         = 0.0
sym.swap_short        = 0.0
sym.swap_mode         = 0

# ─────────────────────────────────────────────────────────────
# Tester Config
# ─────────────────────────────────────────────────────────────
cfg = et.TesterConfig()
cfg.initial_balance  = 10000.0
cfg.leverage         = 100
cfg.tick_mode        = et.TickMode.OPEN_PRICES
cfg.margin_mode      = et.MarginMode.HEDGING
cfg.close_open_at_end= True
cfg.enable_swaps     = False

tester = et.Tester()

# ─────────────────────────────────────────────────────────────
# STRATEGY 1: Pure Python SMA Crossover
# ─────────────────────────────────────────────────────────────
class SmaCrossStrategy(et.Strategy):
    """
    Fast EMA / Slow EMA crossover strategy.
    Written entirely in Python — no changes to the C++ engine required.
    """
    fast_period = 10
    slow_period = 50
    atr_period  = 14
    sl_atr_mult = 2.0
    risk_pct    = 1.0

    def on_init(self):
        self._fast_ema = None
        self._slow_ema = None
        return et.INIT_SUCCEEDED

    def on_bar(self):
        t = self.trade
        bars = t.bars()
        if bars < self.slow_period + 2:
            return

        close = t.iClose(0)
        k_fast = 2.0 / (self.fast_period + 1)
        k_slow = 2.0 / (self.slow_period + 1)

        if self._fast_ema is None:
            self._fast_ema = sum(t.iClose(i) for i in range(self.fast_period)) / self.fast_period
            self._slow_ema = sum(t.iClose(i) for i in range(self.slow_period)) / self.slow_period
        else:
            self._fast_ema += k_fast * (close - self._fast_ema)
            self._slow_ema += k_slow * (close - self._slow_ema)

        # ATR for SL sizing
        atr = sum(
            max(t.iHigh(i) - t.iLow(i),
                abs(t.iHigh(i) - t.iClose(i+1)),
                abs(t.iLow(i) - t.iClose(i+1)))
            for i in range(self.atr_period)
        ) / self.atr_period
        if atr <= 0:
            return

        prev_fast = self._fast_ema - k_fast * (close - self._fast_ema)  # approx
        bull = self._fast_ema > self._slow_ema
        bear = self._fast_ema < self._slow_ema

        # Close opposing positions
        for pos in t.positions():
            is_buy = pos.type == et.PositionType.BUY
            if bear and is_buy:
                t.position_close(pos.ticket)
            elif bull and not is_buy:
                t.position_close(pos.ticket)

        if t.positions_total() > 0:
            return

        # Risk-based lot sizing
        sl_dist = atr * self.sl_atr_mult
        risk_amt = t.account_balance() * (self.risk_pct / 100.0)
        pts_sl = sl_dist / sym.point
        val_pt = sym.trade_tick_value * (sym.point / sym.trade_tick_size)
        lots_raw = risk_amt / (pts_sl * val_pt) if pts_sl * val_pt > 0 else sym.volume_min
        lots = max(sym.volume_min, min(sym.volume_max, round(lots_raw / sym.volume_step) * sym.volume_step))

        if bull:
            t.buy(lots, t.ask() - sl_dist, 0.0, "SmaCross_Buy")
        elif bear:
            t.sell(lots, t.bid() + sl_dist, 0.0, "SmaCross_Sell")


# ─────────────────────────────────────────────────────────────
# STRATEGY 2: Pure Python RSI Strategy
# ─────────────────────────────────────────────────────────────
class RSIStrategy(et.Strategy):
    """RSI mean-reversion: buy oversold, sell overbought."""
    rsi_period  = 14
    oversold    = 30.0
    overbought  = 70.0
    atr_period  = 14
    sl_atr_mult = 1.5
    tp_atr_mult = 3.0
    risk_pct    = 1.0

    def on_init(self):
        self._avg_gain = 0.0
        self._avg_loss = 0.0
        self._rsi_init = False
        self._prev_rsi = 50.0
        return et.INIT_SUCCEEDED

    def _compute_rsi(self):
        t = self.trade
        k = self.rsi_period
        if t.bars() < k + 2:
            return 50.0
        if not self._rsi_init:
            gains, losses = [], []
            for i in range(k):
                d = t.iClose(i) - t.iClose(i + 1)
                (gains if d > 0 else losses).append(abs(d))
            self._avg_gain = sum(gains) / k
            self._avg_loss = sum(losses) / k
            self._rsi_init = True
        else:
            d = t.iClose(0) - t.iClose(1)
            g = d if d > 0 else 0.0
            l = -d if d < 0 else 0.0
            self._avg_gain = (self._avg_gain * (k - 1) + g) / k
            self._avg_loss = (self._avg_loss * (k - 1) + l) / k
        if self._avg_loss < 1e-10:
            return 100.0
        return 100.0 - 100.0 / (1.0 + self._avg_gain / self._avg_loss)

    def on_bar(self):
        t = self.trade
        if t.bars() < self.rsi_period + 2:
            return
        rsi = self._compute_rsi()
        atr = sum(
            max(t.iHigh(i)-t.iLow(i), abs(t.iHigh(i)-t.iClose(i+1)), abs(t.iLow(i)-t.iClose(i+1)))
            for i in range(self.atr_period)
        ) / self.atr_period

        buy_sig  = self._prev_rsi < self.oversold  and rsi >= self.oversold
        sell_sig = self._prev_rsi > self.overbought and rsi <= self.overbought

        for pos in t.positions():
            is_buy = pos.type == et.PositionType.BUY
            if sell_sig and is_buy:  t.position_close(pos.ticket)
            if buy_sig  and not is_buy: t.position_close(pos.ticket)

        self._prev_rsi = rsi
        if t.positions_total() > 0 or atr <= 0:
            return

        sl_dist = atr * self.sl_atr_mult
        tp_dist = atr * self.tp_atr_mult
        risk_amt = t.account_balance() * (self.risk_pct / 100.0)
        pts_sl = sl_dist / sym.point
        val_pt = sym.trade_tick_value * (sym.point / sym.trade_tick_size)
        lots_raw = risk_amt / (pts_sl * val_pt) if pts_sl * val_pt > 0 else sym.volume_min
        lots = max(sym.volume_min, min(sym.volume_max, round(lots_raw / sym.volume_step) * sym.volume_step))

        if buy_sig:
            t.buy(lots, t.ask() - sl_dist, t.ask() + tp_dist, "RSI_Buy")
        elif sell_sig:
            t.sell(lots, t.bid() + sl_dist, t.bid() - tp_dist, "RSI_Sell")


# ─────────────────────────────────────────────────────────────
# Runner
# ─────────────────────────────────────────────────────────────
def run_strategy(name, strategy_cls):
    s = strategy_cls()
    result = tester.run(
        s,
        open_, high, low, close, time, vol, spread,
        sym, cfg
    )
    print(f"\n{'='*60}")
    print(f"  {name}")
    print(f"{'='*60}")
    print(f"  Net Profit:      ${result.net_profit:>10.2f}  ({result.net_profit_pct:+.2f}%)")
    print(f"  Ending Balance:  ${result.ending_balance:>10.2f}")
    print(f"  Total Trades:    {result.total_trades}")
    print(f"  Win Rate:        {result.win_rate*100:.1f}%")
    print(f"  Profit Factor:   {result.profit_factor:.2f}")
    print(f"  Max Drawdown:    {result.max_drawdown_pct:.2f}%  (${result.max_drawdown_abs:.2f})")
    print(f"  Sharpe:          {result.sharpe_ratio:.3f}")
    print(f"  Sortino:         {result.sortino_ratio:.3f}")
    print(f"  Calmar:          {result.calmar_ratio:.3f}")
    print(f"  Commission:      ${result.total_commission:.2f}")
    print(f"  Stop-Outs:       {result.stopout_count}")
    print(f"  Deals (raw):     {len(result.deals)}")
    return result


if __name__ == "__main__":
    r1 = run_strategy("SMA Crossover (10/50 EMA)", SmaCrossStrategy)
    r2 = run_strategy("RSI Mean-Reversion (14, 30/70)", RSIStrategy)

    print("\n\nAll strategies ran successfully on the general-purpose Tester.")
    print("The engine accepted pure Python strategies — no C++ required.\n")
