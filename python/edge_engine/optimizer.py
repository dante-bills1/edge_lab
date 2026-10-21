"""
edge_engine/python/edge_engine/optimizer.py

General-Purpose Strategy Optimizer for arbitrary Strategy classes.
Replicates MetaTrader 5 Strategy Tester Optimization:
- Supports Exhaustive Grid Search & Genetic Algorithm
- Supports all MT5 standard criteria (Balance Max, Profit Factor, Expected Payoff,
  Drawdown Min, Sharpe, Recovery Factor) and Custom (OnTester())
- Multi-threaded / Multi-process execution without GIL contention
"""
from __future__ import annotations

import itertools
import random
from typing import Type, Dict, Any, List, Optional, Callable
from concurrent.futures import ProcessPoolExecutor, as_completed
import numpy as np
import pandas as pd

import edge_tester as et

def _eval_pass(
    strategy_cls: Type[et.Strategy],
    param_dict: Dict[str, Any],
    bars_dict: Dict[str, np.ndarray],
    symbol_dict: Dict[str, Any],
    cfg_dict: Dict[str, Any],
    criterion: et.OptimizationCriterion,
) -> Dict[str, Any]:
    """Single worker evaluation pass."""
    # Instantiate strategy and apply parameters as attributes
    strat = strategy_cls()
    for k, v in param_dict.items():
        setattr(strat, k, v)

    # Build spec
    spec = et.SymbolSpec()
    for k, v in symbol_dict.items():
        if hasattr(spec, k):
            setattr(spec, k, v)

    # Build config
    cfg = et.TesterConfig()
    for k, v in cfg_dict.items():
        if hasattr(cfg, k):
            setattr(cfg, k, v)

    tester = et.Tester()
    res = tester.run(
        strat,
        bars_dict["open"], bars_dict["high"], bars_dict["low"], bars_dict["close"],
        bars_dict["time"], bars_dict["volume"], bars_dict["spread"],
        spec, cfg
    )

    # Compute fitness score based on MT5 OptimizationCriterion
    if criterion == et.OptimizationCriterion.CUSTOM:
        fitness = res.on_tester_value
    elif criterion == et.OptimizationCriterion.BALANCE_MAX:
        fitness = res.ending_balance
    elif criterion == et.OptimizationCriterion.PROFIT_FACTOR:
        fitness = res.profit_factor
    elif criterion == et.OptimizationCriterion.EXPECTED_PAYOFF:
        fitness = res.expected_payoff
    elif criterion == et.OptimizationCriterion.DRAWDOWN_MIN:
        fitness = -res.max_drawdown_pct  # Higher is better
    elif criterion == et.OptimizationCriterion.SHARPE:
        fitness = res.sharpe_ratio
    elif criterion == et.OptimizationCriterion.RECOVERY_FACTOR:
        fitness = res.recovery_factor
    else:
        fitness = res.net_profit_pct

    out = {
        "params": param_dict,
        "fitness": fitness,
        "net_profit": res.net_profit,
        "net_profit_pct": res.net_profit_pct,
        "ending_balance": res.ending_balance,
        "total_trades": res.total_trades,
        "win_rate": res.win_rate,
        "profit_factor": res.profit_factor,
        "expected_payoff": res.expected_payoff,
        "max_drawdown_pct": res.max_drawdown_pct,
        "max_drawdown_abs": res.max_drawdown_abs,
        "recovery_factor": res.recovery_factor,
        "sharpe_ratio": res.sharpe_ratio,
        "sortino_ratio": res.sortino_ratio,
        "calmar_ratio": res.calmar_ratio,
        "on_tester_value": res.on_tester_value,
    }
    return out


class StrategyOptimizer:
    """
    MT5-faithful Strategy Optimizer for arbitrary Strategy classes.
    """
    def __init__(
        self,
        strategy_cls: Type[et.Strategy],
        symbol_spec: et.SymbolSpec,
        tester_config: Optional[et.TesterConfig] = None,
        criterion: et.OptimizationCriterion = et.OptimizationCriterion.BALANCE_MAX,
        algorithm: et.OptimizationAlgorithm = et.OptimizationAlgorithm.EXHAUSTIVE,
        max_workers: int = 4,
    ):
        self.strategy_cls = strategy_cls
        self.spec = symbol_spec
        self.cfg = tester_config or et.TesterConfig()
        self.criterion = criterion
        self.algorithm = algorithm
        self.max_workers = max_workers

    def optimize(
        self,
        df: pd.DataFrame,
        param_grid: Dict[str, List[Any]],
        generations: int = 20,
        population_size: int = 30,
        mutation_rate: float = 0.2,
    ) -> List[Dict[str, Any]]:
        """
        Run the optimization across all CPU cores.
        """
        n = len(df)
        bars_dict = {
            "open": df["open"].to_numpy(np.float64),
            "high": df["high"].to_numpy(np.float64),
            "low": df["low"].to_numpy(np.float64),
            "close": df["close"].to_numpy(np.float64),
            "time": df["time"].to_numpy(np.int64) if "time" in df.columns else np.zeros(n, np.int64),
            "volume": df["tick_volume"].to_numpy(np.int64) if "tick_volume" in df.columns else np.zeros(n, np.int64),
            "spread": df["spread"].to_numpy(np.int32) if "spread" in df.columns else np.zeros(n, np.int32),
        }

        # Serialise spec and cfg
        sym_dict = {
            "name": self.spec.name,
            "point": self.spec.point,
            "digits": self.spec.digits,
            "trade_tick_size": self.spec.trade_tick_size,
            "trade_tick_value": self.spec.trade_tick_value,
            "spread_val": self.spec.spread_val,
            "volume_min": self.spec.volume_min,
            "volume_max": self.spec.volume_max,
            "volume_step": self.spec.volume_step,
            "commission_per_lot": self.spec.commission_per_lot,
        }
        cfg_dict = {
            "initial_balance": self.cfg.initial_balance,
            "leverage": self.cfg.leverage,
            "tick_mode": self.cfg.tick_mode,
            "latency_mode": self.cfg.latency_mode,
            "latency_fixed_ms": self.cfg.latency_fixed_ms,
            "latency_min_ms": self.cfg.latency_min_ms,
            "latency_max_ms": self.cfg.latency_max_ms,
            "latency_seed": self.cfg.latency_seed,
            "close_open_at_end": self.cfg.close_open_at_end,
        }

        if self.algorithm == et.OptimizationAlgorithm.EXHAUSTIVE:
            return self._run_exhaustive(bars_dict, sym_dict, cfg_dict, param_grid)
        else:
            return self._run_genetic(
                bars_dict, sym_dict, cfg_dict, param_grid,
                generations, population_size, mutation_rate
            )

    def _run_exhaustive(
        self,
        bars_dict: Dict[str, np.ndarray],
        sym_dict: Dict[str, Any],
        cfg_dict: Dict[str, Any],
        param_grid: Dict[str, List[Any]],
    ) -> List[Dict[str, Any]]:
        keys = list(param_grid.keys())
        values = list(param_grid.values())
        combos = [dict(zip(keys, p)) for p in itertools.product(*values)]

        results = []
        # Run sequentially or via pool
        if self.max_workers <= 1:
            for p in combos:
                res = _eval_pass(self.strategy_cls, p, bars_dict, sym_dict, cfg_dict, self.criterion)
                results.append(res)
        else:
            with ProcessPoolExecutor(max_workers=self.max_workers) as executor:
                futures = [
                    executor.submit(
                        _eval_pass, self.strategy_cls, p, bars_dict, sym_dict, cfg_dict, self.criterion
                    )
                    for p in combos
                ]
                for f in as_completed(futures):
                    results.append(f.result())

        # Sort descending by fitness
        results.sort(key=lambda x: x["fitness"], reverse=True)
        return results

    def _run_genetic(
        self,
        bars_dict: Dict[str, np.ndarray],
        sym_dict: Dict[str, Any],
        cfg_dict: Dict[str, Any],
        param_grid: Dict[str, List[Any]],
        generations: int,
        population_size: int,
        mutation_rate: float,
    ) -> List[Dict[str, Any]]:
        keys = list(param_grid.keys())
        def random_individual():
            return {k: random.choice(param_grid[k]) for k in keys}

        population = [random_individual() for _ in range(population_size)]
        evaluated_cache = {}
        all_results = []

        def eval_pop(pop):
            unevaluated = [p for p in pop if tuple(sorted(p.items())) not in evaluated_cache]
            if unevaluated:
                if self.max_workers <= 1:
                    for p in unevaluated:
                        res = _eval_pass(self.strategy_cls, p, bars_dict, sym_dict, cfg_dict, self.criterion)
                        k = tuple(sorted(p.items()))
                        evaluated_cache[k] = res
                        all_results.append(res)
                else:
                    with ProcessPoolExecutor(max_workers=self.max_workers) as executor:
                        futures = [
                            executor.submit(
                                _eval_pass, self.strategy_cls, p, bars_dict, sym_dict, cfg_dict, self.criterion
                            )
                            for p in unevaluated
                        ]
                        for f in as_completed(futures):
                            res = f.result()
                            k = tuple(sorted(res["params"].items()))
                            evaluated_cache[k] = res
                            all_results.append(res)
            return [evaluated_cache[tuple(sorted(p.items()))] for p in pop]

        for gen in range(generations):
            scored = eval_pop(population)
            scored.sort(key=lambda x: x["fitness"], reverse=True)

            # Elite selection (top 20%)
            elite_count = max(2, int(population_size * 0.2))
            elites = [s["params"] for s in scored[:elite_count]]

            # Reproduction / Crossover
            next_pop = list(elites)
            while len(next_pop) < population_size:
                p1, p2 = random.sample(elites, 2)
                child = {}
                for k in keys:
                    child[k] = p1[k] if random.random() < 0.5 else p2[k]
                    # Mutation
                    if random.random() < mutation_rate:
                        child[k] = random.choice(param_grid[k])
                next_pop.append(child)

            population = next_pop

        # Final evaluation
        eval_pop(population)
        all_results.sort(key=lambda x: x["fitness"], reverse=True)
        return all_results
