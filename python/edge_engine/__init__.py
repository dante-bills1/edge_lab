"""
edge_engine/python/edge_engine/__init__.py

General-Purpose MetaTrader 5 Strategy Tester Replica (Python Interface).
100% Decoupled from any indicator or strategy.
"""
from __future__ import annotations

import edge_tester as tester_api
from .symbol_spec import make_symbol_spec
from .optimizer import StrategyOptimizer

# Expose core tester classes directly at edge_engine top level
Strategy = tester_api.Strategy
Tester = tester_api.Tester
InteractiveTester = tester_api.InteractiveTester
TesterConfig = tester_api.TesterConfig
TesterResult = tester_api.TesterResult
SymbolSpec = tester_api.SymbolSpec
TickMode = tester_api.TickMode
LatencyMode = tester_api.LatencyMode
OptimizationCriterion = tester_api.OptimizationCriterion
OptimizationAlgorithm = tester_api.OptimizationAlgorithm

__version__ = "2.0.0"
__all__ = [
    "Strategy",
    "Tester",
    "InteractiveTester",
    "TesterConfig",
    "TesterResult",
    "SymbolSpec",
    "TickMode",
    "LatencyMode",
    "OptimizationCriterion",
    "OptimizationAlgorithm",
    "StrategyOptimizer",
    "make_symbol_spec",
    "tester_api",
]
