import time
import pandas as pd
import numpy as np

print("[1] Importing edge_engine...")
import edge_engine

print("[2] Reading parquet...")
df = pd.read_parquet("backtest_suite/historical_data_cache/jump_75_index_100k_m5.parquet").head(100)
df.columns = [c.lower() for c in df.columns]

print(f"[3] Data shape: {df.shape}")
params = {
    "alpha": 0.27, "offset": 3, "confirm_bars": 1,
    "filter_flat_markets": True, "min_slope_threshold": 0.1,
    "ao_min_columns": 1, "cci_length": 41, "cci_smooth": 3,
    "cci_threshold": 60.0, "exit_method": 3,
    "sl_mult": 1.65, "tp_mult": 0.0,
}

print("[4] Calling run_backtest...")
t0 = time.perf_counter()
res = edge_engine.run_backtest(df, params, symbol_name="Jump 75 Index")
elapsed = (time.perf_counter() - t0) * 1000.0
print(f"[5] Done! Elapsed: {elapsed:.2f} ms")
print(f"Total trades: {res['total_trades']}")
print(f"Net profit %: {res['net_profit_pct']:.2f}%")
