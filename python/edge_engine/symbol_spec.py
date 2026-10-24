"""
edge_engine/python/edge_engine/symbol_spec.py
Mirrors backtest_suite/symbol_specs.py but works standalone.
"""
from __future__ import annotations
from typing import Dict, Any, Optional

FALLBACK_SPECS: Dict[str, Dict[str, Any]] = {
    "Jump 75 Index":          {"point": 0.01, "digits": 2, "spread_val": 2.47,  "trade_tick_size": 0.01, "trade_tick_value": 0.01, "volume_min": 0.01, "volume_max": 4.0,   "volume_step": 0.01,  "commission_per_lot": 0.0},
    "Jump 10 Index":          {"point": 0.01, "digits": 2, "spread_val": 2.27,  "trade_tick_size": 0.01, "trade_tick_value": 0.01, "volume_min": 0.01, "volume_max": 10.0,  "volume_step": 0.01,  "commission_per_lot": 0.0},
    "Jump 25 Index":          {"point": 0.01, "digits": 2, "spread_val": 3.20,  "trade_tick_size": 0.01, "trade_tick_value": 0.01, "volume_min": 0.01, "volume_max": 8.0,   "volume_step": 0.01,  "commission_per_lot": 0.0},
    "Jump 50 Index":          {"point": 0.01, "digits": 2, "spread_val": 3.00,  "trade_tick_size": 0.01, "trade_tick_value": 0.01, "volume_min": 0.01, "volume_max": 6.0,   "volume_step": 0.01,  "commission_per_lot": 0.0},
    "Jump 100 Index":         {"point": 0.01, "digits": 2, "spread_val": 0.10,  "trade_tick_size": 0.01, "trade_tick_value": 0.01, "volume_min": 0.01, "volume_max": 2.0,   "volume_step": 0.01,  "commission_per_lot": 0.0},
    "Volatility 75 Index":    {"point": 0.01, "digits": 2, "spread_val": 5.00,  "trade_tick_size": 0.01, "trade_tick_value": 0.01, "volume_min": 0.001,"volume_max": 10.0,  "volume_step": 0.001, "commission_per_lot": 0.0},
    "Volatility 75 (1s) Index":{"point": 0.01,"digits": 2, "spread_val": 4.50, "trade_tick_size": 0.01, "trade_tick_value": 0.01, "volume_min": 0.001,"volume_max": 10.0,  "volume_step": 0.001, "commission_per_lot": 0.0},
    "Volatility 100 Index":   {"point": 0.01, "digits": 2, "spread_val": 0.25,  "trade_tick_size": 0.01, "trade_tick_value": 0.01, "volume_min": 0.2,  "volume_max": 50.0,  "volume_step": 0.01,  "commission_per_lot": 0.0},
    "Volatility 50 Index":    {"point": 0.01, "digits": 2, "spread_val": 1.70,  "trade_tick_size": 0.01, "trade_tick_value": 0.01, "volume_min": 0.01, "volume_max": 10.0,  "volume_step": 0.01,  "commission_per_lot": 0.0},
    "Volatility 25 Index":    {"point": 0.01, "digits": 2, "spread_val": 1.98,  "trade_tick_size": 0.01, "trade_tick_value": 0.01, "volume_min": 0.01, "volume_max": 10.0,  "volume_step": 0.01,  "commission_per_lot": 0.0},
    "Volatility 10 Index":    {"point": 0.001,"digits": 3, "spread_val": 0.158, "trade_tick_size": 0.001,"trade_tick_value": 0.001,"volume_min": 0.1,  "volume_max": 100.0, "volume_step": 0.01,  "commission_per_lot": 0.0},
    "Step Index":             {"point": 0.1,  "digits": 1, "spread_val": 1.00,  "trade_tick_size": 0.1,  "trade_tick_value": 0.1,  "volume_min": 0.1,  "volume_max": 50.0,  "volume_step": 0.01,  "commission_per_lot": 0.0},
}

_GENERIC = {"point": 0.01, "digits": 2, "spread_val": 2.0, "trade_tick_size": 0.01,
            "trade_tick_value": 0.01, "volume_min": 0.01, "volume_max": 10.0,
            "volume_step": 0.01, "commission_per_lot": 0.0,
            "trade_stops_level": 0.0, "trade_freeze_level": 0.0}


_CACHED_SPECS: Dict[str, Dict[str, Any]] = {}


def make_symbol_spec(name: str, try_mt5: bool = False) -> Dict[str, Any]:
    """Return symbol specs, using fast calibrated offline specs by default."""
    clean = name.strip()
    key   = clean.lower().replace(" ", "_").replace("(", "").replace(")", "")

    if key in _CACHED_SPECS:
        return _CACHED_SPECS[key].copy()

    spec = _GENERIC.copy()
    for k, v in FALLBACK_SPECS.items():
        k_n = k.lower().replace(" ", "_").replace("(", "").replace(")", "")
        if k_n in key or key in k_n:
            spec = v.copy()
            break
    spec.setdefault("trade_stops_level",  0.0)
    spec.setdefault("trade_freeze_level", 0.0)
    spec.setdefault("swap_long",  0.0)
    spec.setdefault("swap_short", 0.0)

    if try_mt5:
        try:
            import MetaTrader5 as mt5
            if mt5.initialize():
                mt5.symbol_select(clean, True)
                info = mt5.symbol_info(clean)
                if info:
                    p = float(info.point)
                    spec["point"]               = p
                    spec["digits"]              = int(info.digits)
                    spec["spread_val"]          = int(info.spread) * p
                    spec["trade_tick_size"]     = float(info.trade_tick_size)
                    spec["trade_tick_value"]    = float(info.trade_tick_value)
                    spec["volume_min"]          = float(info.volume_min)
                    spec["volume_max"]          = float(info.volume_max)
                    spec["volume_step"]         = float(info.volume_step)
                    spec["trade_stops_level"]   = float(getattr(info, "trade_stops_level", 0.0))
                    spec["trade_freeze_level"]  = float(getattr(info, "trade_freeze_level", 0.0))
                mt5.shutdown()
        except Exception:
            pass
    return spec
