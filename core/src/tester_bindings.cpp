/**
 * EdgeEngine - Open-Source MT5 Strategy Tester Replica
 * tester_bindings.cpp
 *
 * pybind11 bindings that expose the GENERAL-PURPOSE Tester API to Python.
 *
 * Design Pattern: Python Strategy via Trampoline
 * Python users define strategies by subclassing PyStrategy (which inherits
 * from edge::Strategy) and overriding OnInit/OnTick/OnBar/OnDeinit.
 *
 * Python usage example:
 *
 *   import edge_tester as et
 *
 *   class MyStrategy(et.Strategy):
 *       def on_init(self):
 *           self.fast = 10
 *           self.slow = 30
 *           return 0  # INIT_SUCCEEDED
 *
 *       def on_bar(self):
 *           close = [self.trade.iClose(i) for i in range(self.slow)]
 *           if len(close) < self.slow: return
 *           fast_sma = sum(close[:self.fast]) / self.fast
 *           slow_sma = sum(close[:self.slow]) / self.slow
 *           if fast_sma > slow_sma and self.trade.positions_total() == 0:
 *               self.trade.buy(0.01, 0.0, 0.0)
 *
 *   sym = et.SymbolSpec()
 *   sym.point = 0.01
 *   ...
 *
 *   cfg = et.TesterConfig()
 *   cfg.initial_balance = 10000
 *   cfg.tick_mode = et.TickMode.OPEN_PRICES
 *
 *   tester = et.Tester()
 *   result = tester.run(MyStrategy(), bars_df, sym, cfg)
 *   print(result.net_profit_pct, result.sharpe_ratio, result.total_trades)
 */
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "../include/mql_types.hpp"
#include "../include/account.hpp"
#include "../include/order_book.hpp"
#include "../include/strategy_base.hpp"
#include "../include/tick_generator.hpp"
#include "../include/tester.hpp"
#include "../include/types.hpp"

namespace py = pybind11;
using namespace edge;

// ============================================================
// Trampoline class: allows Python to subclass Strategy
// Supports both snake_case (Pythonic) and PascalCase (MQL5)
// ============================================================
class PyStrategy : public Strategy {
public:
    using Strategy::Strategy;

    int OnInit() override {
        PYBIND11_OVERRIDE_IMPL(int, Strategy, "on_init");
        PYBIND11_OVERRIDE_IMPL(int, Strategy, "OnInit");
        return Strategy::OnInit();
    }
    void OnDeinit(ENUM_DEINIT_REASON reason) override {
        PYBIND11_OVERRIDE_IMPL(void, Strategy, "on_deinit", reason);
        PYBIND11_OVERRIDE_IMPL(void, Strategy, "OnDeinit", reason);
        Strategy::OnDeinit(reason);
    }
    void OnTick() override {
        PYBIND11_OVERRIDE_IMPL(void, Strategy, "on_tick");
        PYBIND11_OVERRIDE_IMPL(void, Strategy, "OnTick");
        Strategy::OnTick();
    }
    void OnBar() override {
        PYBIND11_OVERRIDE_IMPL(void, Strategy, "on_bar");
        PYBIND11_OVERRIDE_IMPL(void, Strategy, "OnBar");
        Strategy::OnBar();
    }
    void OnTradeTransaction(
        const MqlTradeTransaction& trans,
        const MqlTradeRequest& req,
        const MqlTradeResult& res) override
    {
        PYBIND11_OVERRIDE_IMPL(void, Strategy, "on_trade_transaction", trans, req, res);
        PYBIND11_OVERRIDE_IMPL(void, Strategy, "OnTradeTransaction", trans, req, res);
        Strategy::OnTradeTransaction(trans, req, res);
    }
    double OnTester() override {
        PYBIND11_OVERRIDE_IMPL(double, Strategy, "on_tester");
        PYBIND11_OVERRIDE_IMPL(double, Strategy, "OnTester");
        return Strategy::OnTester();
    }
};

// Helper: build MqlRates vector from numpy arrays
static std::vector<MqlRates> rates_from_arrays(
    py::array_t<double>  open_arr,
    py::array_t<double>  high_arr,
    py::array_t<double>  low_arr,
    py::array_t<double>  close_arr,
    py::array_t<int64_t> time_arr,
    py::array_t<int64_t> volume_arr,
    py::array_t<int32_t> spread_arr
) {
    auto o  = open_arr.unchecked<1>();
    auto h  = high_arr.unchecked<1>();
    auto l  = low_arr.unchecked<1>();
    auto c  = close_arr.unchecked<1>();
    auto t  = time_arr.unchecked<1>();
    auto v  = volume_arr.unchecked<1>();
    auto sp = spread_arr.unchecked<1>();
    int32_t n = static_cast<int32_t>(o.shape(0));

    std::vector<MqlRates> bars(n);
    for (int32_t i = 0; i < n; ++i) {
        bars[i].time        = t(i);
        bars[i].open        = o(i);
        bars[i].high        = h(i);
        bars[i].low         = l(i);
        bars[i].close       = c(i);
        bars[i].tick_volume = v(i);
        bars[i].spread      = sp(i);
        bars[i].real_volume = 0;
    }
    return bars;
}

PYBIND11_MODULE(edge_tester, m) {
    m.doc() = "EdgeEngine General-Purpose MT5 Strategy Tester (C++ core)";

    // ---- Enums ----
    py::enum_<TickMode>(m, "TickMode", py::module_local())
        .value("OPEN_PRICES", TickMode::OPEN_PRICES)
        .value("EVERY_TICK",  TickMode::EVERY_TICK)
        .value("REAL_TICKS",  TickMode::REAL_TICKS)
        .value("OHLC_M1",     TickMode::OHLC_M1);

    py::enum_<LatencyMode>(m, "LatencyMode")
        .value("NONE",   LatencyMode::NONE)
        .value("FIXED",  LatencyMode::FIXED)
        .value("RANDOM", LatencyMode::RANDOM);

    py::enum_<OptimizationCriterion>(m, "OptimizationCriterion")
        .value("CUSTOM",          OptimizationCriterion::CUSTOM)
        .value("BALANCE_MAX",     OptimizationCriterion::BALANCE_MAX)
        .value("PROFIT_FACTOR",   OptimizationCriterion::PROFIT_FACTOR)
        .value("EXPECTED_PAYOFF", OptimizationCriterion::EXPECTED_PAYOFF)
        .value("DRAWDOWN_MIN",    OptimizationCriterion::DRAWDOWN_MIN)
        .value("SHARPE",          OptimizationCriterion::SHARPE)
        .value("RECOVERY_FACTOR", OptimizationCriterion::RECOVERY_FACTOR);

    py::enum_<OptimizationAlgorithm>(m, "OptimizationAlgorithm")
        .value("EXHAUSTIVE", OptimizationAlgorithm::EXHAUSTIVE)
        .value("GENETIC",    OptimizationAlgorithm::GENETIC);

    py::enum_<ENUM_ACCOUNT_MARGIN_MODE>(m, "MarginMode")
        .value("NETTING", ACCOUNT_MARGIN_MODE_RETAIL_NETTING)
        .value("HEDGING", ACCOUNT_MARGIN_MODE_RETAIL_HEDGING)
        .value("EXCHANGE", ACCOUNT_MARGIN_MODE_EXCHANGE);

    py::enum_<ENUM_ORDER_TYPE>(m, "OrderType")
        .value("BUY",              ORDER_TYPE_BUY)
        .value("SELL",             ORDER_TYPE_SELL)
        .value("BUY_LIMIT",        ORDER_TYPE_BUY_LIMIT)
        .value("SELL_LIMIT",       ORDER_TYPE_SELL_LIMIT)
        .value("BUY_STOP",         ORDER_TYPE_BUY_STOP)
        .value("SELL_STOP",        ORDER_TYPE_SELL_STOP)
        .value("BUY_STOP_LIMIT",   ORDER_TYPE_BUY_STOP_LIMIT)
        .value("SELL_STOP_LIMIT",  ORDER_TYPE_SELL_STOP_LIMIT);

    py::enum_<ENUM_POSITION_TYPE>(m, "PositionType")
        .value("BUY",  POSITION_TYPE_BUY)
        .value("SELL", POSITION_TYPE_SELL);

    py::enum_<ENUM_DEAL_ENTRY>(m, "DealEntry")
        .value("IN",     DEAL_ENTRY_IN)
        .value("OUT",    DEAL_ENTRY_OUT)
        .value("INOUT",  DEAL_ENTRY_INOUT)
        .value("OUT_BY", DEAL_ENTRY_OUT_BY);

    py::enum_<ENUM_DEAL_REASON>(m, "DealReason")
        .value("CLIENT", DEAL_REASON_CLIENT)
        .value("EXPERT", DEAL_REASON_EXPERT)
        .value("SL",     DEAL_REASON_SL)
        .value("TP",     DEAL_REASON_TP)
        .value("SO",     DEAL_REASON_SO);

    py::enum_<ENUM_DEINIT_REASON>(m, "DeinitReason")
        .value("PROGRAM",      REASON_PROGRAM)
        .value("REMOVE",       REASON_REMOVE)
        .value("PARAMETERS",   REASON_PARAMETERS)
        .value("CLOSE",        REASON_CLOSE)
        .value("INITFAILED",   REASON_INITFAILED);

    // ---- SymbolSpec ----
    py::class_<SymbolSpec>(m, "SymbolSpec", py::module_local())
        .def(py::init<>())
        .def_readwrite("name",                 &SymbolSpec::name)
        .def_readwrite("point",                &SymbolSpec::point)
        .def_readwrite("digits",               &SymbolSpec::digits)
        .def_readwrite("trade_tick_size",      &SymbolSpec::trade_tick_size)
        .def_readwrite("trade_tick_value",     &SymbolSpec::trade_tick_value)
        .def_readwrite("spread_val",           &SymbolSpec::spread_val)
        .def_readwrite("spread_points",        &SymbolSpec::spread_points)
        .def_readwrite("volume_min",           &SymbolSpec::volume_min)
        .def_readwrite("volume_max",           &SymbolSpec::volume_max)
        .def_readwrite("volume_step",          &SymbolSpec::volume_step)
        .def_readwrite("commission_per_lot",   &SymbolSpec::commission_per_lot)
        .def_readwrite("trade_stops_level",    &SymbolSpec::trade_stops_level)
        .def_readwrite("trade_freeze_level",   &SymbolSpec::trade_freeze_level)
        .def_readwrite("swap_long",            &SymbolSpec::swap_long)
        .def_readwrite("swap_short",           &SymbolSpec::swap_short)
        .def_readwrite("swap_mode",            &SymbolSpec::swap_mode);

    // ---- TesterConfig ----
    py::class_<TesterConfig>(m, "TesterConfig")
        .def(py::init<>())
        .def_readwrite("initial_balance",       &TesterConfig::initial_balance)
        .def_readwrite("leverage",              &TesterConfig::leverage)
        .def_readwrite("tick_mode",             &TesterConfig::tick_mode)
        .def_readwrite("margin_mode",           &TesterConfig::margin_mode)
        .def_readwrite("margin_call_level",     &TesterConfig::margin_call_level)
        .def_readwrite("margin_stopout_level",  &TesterConfig::margin_stopout_level)
        .def_readwrite("close_open_at_end",     &TesterConfig::close_open_at_end)
        .def_readwrite("enable_swaps",          &TesterConfig::enable_swaps)
        .def_readwrite("commission_per_lot",    &TesterConfig::commission_per_lot)
        .def_readwrite("latency_mode",          &TesterConfig::latency_mode)
        .def_readwrite("latency_fixed_ms",      &TesterConfig::latency_fixed_ms)
        .def_readwrite("latency_min_ms",        &TesterConfig::latency_min_ms)
        .def_readwrite("latency_max_ms",        &TesterConfig::latency_max_ms)
        .def_readwrite("latency_seed",          &TesterConfig::latency_seed)
        .def_readwrite("verbose",               &TesterConfig::verbose);

    // ---- MqlTick ----
    py::class_<MqlTick>(m, "MqlTick")
        .def_readwrite("time",     &MqlTick::time)
        .def_readwrite("bid",      &MqlTick::bid)
        .def_readwrite("ask",      &MqlTick::ask)
        .def_readwrite("last",     &MqlTick::last)
        .def_readwrite("volume",   &MqlTick::volume)
        .def_readwrite("time_msc", &MqlTick::time_msc)
        .def_readwrite("flags",    &MqlTick::flags);

    // ---- MqlRates (Bar) ----
    py::class_<MqlRates>(m, "MqlRates")
        .def(py::init<>())
        .def_readwrite("time",        &MqlRates::time)
        .def_readwrite("open",        &MqlRates::open)
        .def_readwrite("high",        &MqlRates::high)
        .def_readwrite("low",         &MqlRates::low)
        .def_readwrite("close",       &MqlRates::close)
        .def_readwrite("tick_volume", &MqlRates::tick_volume)
        .def_readwrite("spread",      &MqlRates::spread);

    // ---- PositionEntity ----
    py::class_<PositionEntity>(m, "Position")
        .def_readonly("ticket",        &PositionEntity::ticket)
        .def_readonly("time",          &PositionEntity::time)
        .def_readonly("type",          &PositionEntity::type)
        .def_readonly("magic",         &PositionEntity::magic)
        .def_readonly("volume",        &PositionEntity::volume)
        .def_readonly("price_open",    &PositionEntity::price_open)
        .def_readonly("price_current", &PositionEntity::price_current)
        .def_readonly("sl",            &PositionEntity::sl)
        .def_readonly("tp",            &PositionEntity::tp)
        .def_readonly("profit",        &PositionEntity::profit)
        .def_readonly("swap",          &PositionEntity::swap)
        .def_readonly("symbol",        &PositionEntity::symbol)
        .def_readonly("comment",       &PositionEntity::comment)
        .def_readonly("mfe",           &PositionEntity::mfe)
        .def_readonly("mae",           &PositionEntity::mae);

    // ---- Order ----
    py::class_<Order>(m, "Order")
        .def_readonly("ticket",           &Order::ticket)
        .def_readonly("time_setup",       &Order::time_setup)
        .def_readonly("type",             &Order::type)
        .def_readonly("state",            &Order::state)
        .def_readonly("price_open",       &Order::price_open)
        .def_readonly("price_stoplimit",  &Order::price_stoplimit)
        .def_readonly("sl",               &Order::sl)
        .def_readonly("tp",               &Order::tp)
        .def_readonly("volume_initial",   &Order::volume_initial)
        .def_readonly("volume_current",   &Order::volume_current)
        .def_readonly("symbol",           &Order::symbol)
        .def_readonly("comment",          &Order::comment)
        .def_readonly("magic",            &Order::magic);

    // ---- InFlightOrder (Execution Delay Queue) ----
    py::class_<InFlightOrder>(m, "InFlightOrder")
        .def_readonly("ticket",          &InFlightOrder::ticket)
        .def_readonly("submit_time_msc", &InFlightOrder::submit_time_msc)
        .def_readonly("target_time_msc", &InFlightOrder::target_time_msc)
        .def_readonly("submit_bar",      &InFlightOrder::submit_bar);

    // ---- Deal ----
    py::class_<Deal>(m, "Deal")
        .def_readonly("ticket",      &Deal::ticket)
        .def_readonly("order",       &Deal::order)
        .def_readonly("time",        &Deal::time)
        .def_readonly("type",        &Deal::type)
        .def_readonly("entry",       &Deal::entry)
        .def_readonly("reason",      &Deal::reason)
        .def_readonly("position_id", &Deal::position_id)
        .def_readonly("volume",      &Deal::volume)
        .def_readonly("price",       &Deal::price)
        .def_readonly("commission",  &Deal::commission)
        .def_readonly("swap",        &Deal::swap)
        .def_readonly("profit",      &Deal::profit)
        .def_readonly("sl",          &Deal::sl)
        .def_readonly("tp",          &Deal::tp)
        .def_readonly("symbol",      &Deal::symbol)
        .def_readonly("comment",     &Deal::comment);

    // ---- MqlTradeTransaction ----
    py::class_<MqlTradeTransaction>(m, "TradeTransaction")
        .def_readonly("deal",        &MqlTradeTransaction::deal)
        .def_readonly("order",       &MqlTradeTransaction::order)
        .def_readonly("symbol",      &MqlTradeTransaction::symbol)
        .def_readonly("type",        &MqlTradeTransaction::type)
        .def_readonly("deal_type",   &MqlTradeTransaction::deal_type)
        .def_readonly("price",       &MqlTradeTransaction::price)
        .def_readonly("volume",      &MqlTradeTransaction::volume)
        .def_readonly("position",    &MqlTradeTransaction::position);

    // ---- MqlTradeRequest ----
    py::class_<MqlTradeRequest>(m, "TradeRequest")
        .def(py::init<>())
        .def_readwrite("action",   &MqlTradeRequest::action)
        .def_readwrite("magic",    &MqlTradeRequest::magic)
        .def_readwrite("symbol",   &MqlTradeRequest::symbol)
        .def_readwrite("volume",   &MqlTradeRequest::volume)
        .def_readwrite("price",    &MqlTradeRequest::price)
        .def_readwrite("sl",       &MqlTradeRequest::sl)
        .def_readwrite("tp",       &MqlTradeRequest::tp)
        .def_readwrite("type",     &MqlTradeRequest::type)
        .def_readwrite("comment",  &MqlTradeRequest::comment);

    // ---- MqlTradeResult ----
    py::class_<MqlTradeResult>(m, "TradeResult")
        .def_readonly("retcode", &MqlTradeResult::retcode)
        .def_readonly("deal",    &MqlTradeResult::deal)
        .def_readonly("order",   &MqlTradeResult::order)
        .def_readonly("volume",  &MqlTradeResult::volume)
        .def_readonly("price",   &MqlTradeResult::price)
        .def_readonly("comment", &MqlTradeResult::comment);

    // ---- TradeContext ----
    py::class_<TradeContext>(m, "TradeContext")
        // Market orders
        .def("buy",  &TradeContext::Buy,
             py::arg("volume"), py::arg("sl")=0.0, py::arg("tp")=0.0,
             py::arg("comment")="", py::arg("magic")=0)
        .def("sell", &TradeContext::Sell,
             py::arg("volume"), py::arg("sl")=0.0, py::arg("tp")=0.0,
             py::arg("comment")="", py::arg("magic")=0)
        // Pending orders
        .def("buy_limit",  &TradeContext::BuyLimit,
             py::arg("volume"), py::arg("price"), py::arg("sl")=0.0,
             py::arg("tp")=0.0, py::arg("expiry")=0, py::arg("comment")="", py::arg("magic")=0)
        .def("sell_limit", &TradeContext::SellLimit,
             py::arg("volume"), py::arg("price"), py::arg("sl")=0.0,
             py::arg("tp")=0.0, py::arg("expiry")=0, py::arg("comment")="", py::arg("magic")=0)
        .def("buy_stop",   &TradeContext::BuyStop,
             py::arg("volume"), py::arg("price"), py::arg("sl")=0.0,
             py::arg("tp")=0.0, py::arg("expiry")=0, py::arg("comment")="", py::arg("magic")=0)
        .def("sell_stop",  &TradeContext::SellStop,
             py::arg("volume"), py::arg("price"), py::arg("sl")=0.0,
             py::arg("tp")=0.0, py::arg("expiry")=0, py::arg("comment")="", py::arg("magic")=0)
        // Position/Order management
        .def("position_close",  &TradeContext::PositionClose,
             py::arg("ticket"), py::arg("volume")=0.0)
        .def("position_modify", &TradeContext::PositionModify,
             py::arg("ticket"), py::arg("sl"), py::arg("tp"))
        .def("order_modify",
             [](TradeContext& tc, uint64_t ticket, double price,
                double sl, double tp, py::object time_type_obj, int64_t expiry) {
                 ENUM_ORDER_TYPE_TIME tt = ORDER_TIME_GTC;
                 if (!time_type_obj.is_none())
                     tt = time_type_obj.cast<ENUM_ORDER_TYPE_TIME>();
                 return tc.OrderModify(ticket, price, sl, tp, tt, expiry);
             },
             py::arg("ticket"), py::arg("price"), py::arg("sl"), py::arg("tp"),
             py::arg("time_type")=py::none(), py::arg("expiry")=0)
        .def("order_delete",    &TradeContext::OrderDelete, py::arg("ticket"))
        // Accessors
        .def("ask",              &TradeContext::Ask)
        .def("bid",              &TradeContext::Bid)
        .def("spread",           &TradeContext::Spread)
        .def("account_balance",  &TradeContext::AccountBalance)
        .def("account_equity",   &TradeContext::AccountEquity)
        .def("account_free_margin", &TradeContext::AccountFreeMargin)
        .def("account_margin_level", &TradeContext::AccountMarginLevel)
        .def("positions_total",  &TradeContext::PositionsTotal)
        .def("orders_total",     &TradeContext::OrdersTotal)
        .def("positions",        &TradeContext::Positions, py::return_value_policy::reference)
        .def("orders",           &TradeContext::Orders,    py::return_value_policy::reference)
        // Historical prices (MQL5 iClose / iOpen etc.)
        .def("iClose",  &TradeContext::iClose,  py::arg("shift")=0)
        .def("iOpen",   &TradeContext::iOpen,   py::arg("shift")=0)
        .def("iHigh",   &TradeContext::iHigh,   py::arg("shift")=0)
        .def("iLow",    &TradeContext::iLow,    py::arg("shift")=0)
        .def("iTime",   &TradeContext::iTime,   py::arg("shift")=0)
        .def("iVolume", &TradeContext::iVolume, py::arg("shift")=0)
        .def("bars",    &TradeContext::BarsCount)
        .def("result_last", &TradeContext::ResultLast, py::return_value_policy::reference);

    // ---- TesterResult ----
    py::class_<TesterResult>(m, "TesterResult")
        .def_readonly("net_profit",          &TesterResult::net_profit)
        .def_readonly("net_profit_pct",      &TesterResult::net_profit_pct)
        .def_readonly("gross_profit",        &TesterResult::gross_profit)
        .def_readonly("gross_loss",          &TesterResult::gross_loss)
        .def_readonly("ending_balance",      &TesterResult::ending_balance)
        .def_readonly("initial_balance",     &TesterResult::initial_balance)
        .def_readonly("total_trades",        &TesterResult::total_trades)
        .def_readonly("winning_trades",      &TesterResult::winning_trades)
        .def_readonly("losing_trades",       &TesterResult::losing_trades)
        .def_readonly("win_rate",            &TesterResult::win_rate)
        .def_readonly("profit_factor",       &TesterResult::profit_factor)
        .def_readonly("expected_payoff",     &TesterResult::expected_payoff)
        .def_readonly("avg_profit_trade",    &TesterResult::avg_profit_trade)
        .def_readonly("avg_loss_trade",      &TesterResult::avg_loss_trade)
        .def_readonly("max_drawdown_pct",    &TesterResult::max_drawdown_pct)
        .def_readonly("max_drawdown_abs",    &TesterResult::max_drawdown_abs)
        .def_readonly("recovery_factor",     &TesterResult::recovery_factor)
        .def_readonly("sharpe_ratio",        &TesterResult::sharpe_ratio)
        .def_readonly("sortino_ratio",       &TesterResult::sortino_ratio)
        .def_readonly("calmar_ratio",        &TesterResult::calmar_ratio)
        .def_readonly("on_tester_value",     &TesterResult::on_tester_value)
        .def_readonly("stopout_count",       &TesterResult::stopout_count)
        .def_readonly("total_commission",    &TesterResult::total_commission)
        .def_readonly("total_swap",          &TesterResult::total_swap)
        .def_readonly("deals",               &TesterResult::deals)
        .def_property_readonly("balance_curve", [](const TesterResult& r) {
            return py::array_t<double>(r.balance_curve.size(), r.balance_curve.data());
        })
        .def_property_readonly("equity_curve", [](const TesterResult& r) {
            return py::array_t<double>(r.equity_curve.size(), r.equity_curve.data());
        })
        .def("to_dict", [](const TesterResult& r) {
            py::dict d;
            d["net_profit"]       = r.net_profit;
            d["net_profit_pct"]   = r.net_profit_pct;
            d["ending_balance"]   = r.ending_balance;
            d["total_trades"]     = r.total_trades;
            d["winning_trades"]   = r.winning_trades;
            d["losing_trades"]    = r.losing_trades;
            d["win_rate"]         = r.win_rate;
            d["profit_factor"]    = r.profit_factor;
            d["expected_payoff"]  = r.expected_payoff;
            d["max_drawdown_pct"] = r.max_drawdown_pct;
            d["max_drawdown_abs"] = r.max_drawdown_abs;
            d["recovery_factor"]  = r.recovery_factor;
            d["sharpe_ratio"]     = r.sharpe_ratio;
            d["sortino_ratio"]    = r.sortino_ratio;
            d["calmar_ratio"]     = r.calmar_ratio;
            d["on_tester_value"]  = r.on_tester_value;
            d["total_commission"] = r.total_commission;
            d["total_swap"]       = r.total_swap;
            d["stopout_count"]    = r.stopout_count;
            return d;
        });

    // ---- Abstract Strategy (with Python trampoline) ----
    py::class_<Strategy, PyStrategy>(m, "Strategy")
        .def(py::init<>())
        .def("on_init",  &Strategy::OnInit)
        .def("OnInit",   &Strategy::OnInit)
        .def("on_deinit",&Strategy::OnDeinit)
        .def("OnDeinit", &Strategy::OnDeinit)
        .def("on_tick",  &Strategy::OnTick)
        .def("OnTick",   &Strategy::OnTick)
        .def("on_bar",   &Strategy::OnBar)
        .def("OnBar",    &Strategy::OnBar)
        .def("on_trade_transaction", &Strategy::OnTradeTransaction)
        .def("OnTradeTransaction",   &Strategy::OnTradeTransaction)
        .def("on_tester", &Strategy::OnTester)
        .def("OnTester",  &Strategy::OnTester)
        // Inject trade context + context as Python attributes
        .def_property_readonly("trade", [](Strategy* s) {
            return s->Trade;
        }, py::return_value_policy::reference)
        .def_property_readonly("Trade", [](Strategy* s) {
            return s->Trade;
        }, py::return_value_policy::reference)
        .def_property_readonly("ctx_bar", [](const Strategy* s) {
            return s->ctx.current_bar;
        })
        .def_property_readonly("ctx_total_bars", [](const Strategy* s) {
            return s->ctx.total_bars;
        })
        .def_property_readonly("ctx_tick", [](const Strategy* s) {
            return s->ctx.current_tick;
        });

    // ---- Tester ----
    py::class_<Tester>(m, "Tester")
        .def(py::init<>())
        .def("run",
            [](const Tester& tester,
               Strategy* strategy,
               py::array_t<double>  open_a,
               py::array_t<double>  high_a,
               py::array_t<double>  low_a,
               py::array_t<double>  close_a,
               py::array_t<int64_t> time_a,
               py::array_t<int64_t> volume_a,
               py::array_t<int32_t> spread_a,
               const SymbolSpec& sym,
               py::object cfg_obj,
               py::object spread_series_obj) -> TesterResult
            {
                TesterConfig cfg{};
                if (!cfg_obj.is_none())
                    cfg = cfg_obj.cast<TesterConfig>();
                auto bars = rates_from_arrays(
                    open_a, high_a, low_a, close_a, time_a, volume_a, spread_a);
                int32_t n = static_cast<int32_t>(bars.size());

                std::vector<double> ss_buf;
                const double* ss_ptr = nullptr;
                if (!spread_series_obj.is_none()) {
                    auto ss = spread_series_obj.cast<py::array_t<double>>();
                    auto r  = ss.unchecked<1>();
                    ss_buf.resize(n);
                    for (int32_t i = 0; i < n; ++i) ss_buf[i] = r(i);
                    ss_ptr = ss_buf.data();
                }
                return tester.run(strategy, bars.data(), n, sym, cfg, ss_ptr);
            },
            py::arg("strategy"),
            py::arg("open"), py::arg("high"), py::arg("low"), py::arg("close"),
            py::arg("time"), py::arg("volume"), py::arg("spread"),
            py::arg("symbol"),
            py::arg("cfg") = py::none(),
            py::arg("spread_series") = py::none(),
            "Run a full backtest simulation for the given Strategy subclass.");

    // ---- InteractiveTester (Visual / Step-by-Step Replay Engine) ----
    py::class_<InteractiveTester>(m, "InteractiveTester")
        .def(py::init<>())
        .def("init",
            [](InteractiveTester& self,
               Strategy* strategy,
               py::array_t<double>  open_a,
               py::array_t<double>  high_a,
               py::array_t<double>  low_a,
               py::array_t<double>  close_a,
               py::array_t<int64_t> time_a,
               py::array_t<int64_t> volume_a,
               py::array_t<int32_t> spread_a,
               const SymbolSpec& sym,
               py::object cfg_obj,
               py::object spread_series_obj) -> bool
            {
                TesterConfig cfg{};
                if (!cfg_obj.is_none())
                    cfg = cfg_obj.cast<TesterConfig>();
                auto bars = rates_from_arrays(
                    open_a, high_a, low_a, close_a, time_a, volume_a, spread_a);
                int32_t n = static_cast<int32_t>(bars.size());

                std::vector<double> ss_buf;
                if (!spread_series_obj.is_none()) {
                    auto ss = spread_series_obj.cast<py::array_t<double>>();
                    auto r  = ss.unchecked<1>();
                    ss_buf.resize(n);
                    for (int32_t i = 0; i < n; ++i) ss_buf[i] = r(i);
                }
                return self.init(strategy, std::move(bars), sym, cfg, std::move(ss_buf));
            },
            py::arg("strategy"),
            py::arg("open"), py::arg("high"), py::arg("low"), py::arg("close"),
            py::arg("time"), py::arg("volume"), py::arg("spread"),
            py::arg("symbol"),
            py::arg("cfg") = py::none(),
            py::arg("spread_series") = py::none())
        .def("step_tick",            &InteractiveTester::step_tick)
        .def("step_bar",             &InteractiveTester::step_bar)
        .def("run_all",              &InteractiveTester::run_all)
        .def("finish",               &InteractiveTester::finish)
        .def("is_done",              &InteractiveTester::is_done)
        .def("current_bar",          &InteractiveTester::current_bar)
        .def("total_bars",           &InteractiveTester::total_bars)
        .def("current_tick",         &InteractiveTester::current_tick)
        .def("account_balance",      &InteractiveTester::account_balance)
        .def("account_equity",       &InteractiveTester::account_equity)
        .def("account_margin",       &InteractiveTester::account_margin)
        .def("account_free_margin",  &InteractiveTester::account_free_margin)
        .def("account_margin_level", &InteractiveTester::account_margin_level)
        .def("positions_total",      &InteractiveTester::positions_total)
        .def("orders_total",         &InteractiveTester::orders_total)
        .def("positions",            &InteractiveTester::positions, py::return_value_policy::reference)
        .def("orders",               &InteractiveTester::orders,    py::return_value_policy::reference)
        .def("deals",                &InteractiveTester::deals,     py::return_value_policy::reference)
        .def("in_flight_orders",     &InteractiveTester::in_flight_orders, py::return_value_policy::reference);

    // ---- Utility ----
    m.def("rates_from_arrays", &rates_from_arrays,
        py::arg("open"), py::arg("high"), py::arg("low"), py::arg("close"),
        py::arg("time"), py::arg("volume"), py::arg("spread"),
        "Convert numpy arrays to a list of MqlRates bar structs.");

    m.attr("INIT_SUCCEEDED") = INIT_SUCCEEDED;
    m.attr("INIT_FAILED")    = INIT_FAILED;
    m.attr("__version__")    = "2.0.0";
}
