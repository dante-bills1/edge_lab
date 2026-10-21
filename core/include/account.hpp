/**
 * EdgeEngine - Open-Source MT5 Strategy Tester Replica
 * account.hpp - 1:1 simulation of MT5 trading account and margin engine.
 */
#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include "mql_types.hpp"
#include "types.hpp"

namespace edge {

struct AccountConfig {
    double                   initial_balance     = 10000.0;
    std::string              currency            = "USD";
    int32_t                  leverage            = 100;
    ENUM_ACCOUNT_MARGIN_MODE margin_mode         = ACCOUNT_MARGIN_MODE_RETAIL_HEDGING;
    double                   margin_call_level   = 100.0; // 100%
    double                   margin_stopout_level= 50.0;  // 50%
};

class Account {
public:
    explicit Account(const AccountConfig& cfg = AccountConfig{})
        : config_(cfg),
          balance_(cfg.initial_balance),
          equity_(cfg.initial_balance),
          margin_(0.0),
          free_margin_(cfg.initial_balance),
          margin_level_(0.0),
          stopout_count_(0),
          margin_call_count_(0)
    {}

    void reset() {
        balance_            = config_.initial_balance;
        equity_             = config_.initial_balance;
        margin_             = 0.0;
        free_margin_        = config_.initial_balance;
        margin_level_       = 0.0;
        stopout_count_      = 0;
        margin_call_count_  = 0;
    }

    // --- State Getters ---
    double balance() const     { return balance_; }
    double equity() const      { return equity_; }
    double margin() const       { return margin_; }
    double free_margin() const  { return free_margin_; }
    double margin_level() const { return margin_level_; }
    int32_t leverage() const    { return config_.leverage; }
    ENUM_ACCOUNT_MARGIN_MODE margin_mode() const { return config_.margin_mode; }
    const std::string& currency() const { return config_.currency; }
    int32_t stopout_count() const { return stopout_count_; }
    int32_t margin_call_count() const { return margin_call_count_; }

    // --- Balance Operations ---
    void deposit(double amount) {
        balance_ += amount;
        equity_  += amount;
        free_margin_ = equity_ - margin_;
    }

    void apply_pnl(double net_pnl) {
        balance_ += net_pnl;
        equity_  += net_pnl;
        free_margin_ = equity_ - margin_;
        _recalculate_margin_level();
    }

    // --- Margin Calculation ---
    double calculate_required_margin(const SymbolSpec& sym, double lots, double price) const {
        if (config_.leverage <= 0) return 0.0;
        // Standard Forex/CFD margin formula: (Lots * ContractSize * Price) / Leverage
        // If contract size not set, assume standard 100,000 for 5-digit forex or calculate from tick value
        double contract_size = 100000.0;
        if (sym.trade_tick_size > EPSILON_PRICE && sym.trade_tick_value > EPSILON_PRICE) {
            contract_size = sym.trade_tick_value / sym.trade_tick_size;
        }
        return (lots * contract_size * price) / static_cast<double>(config_.leverage);
    }

    // Verify if account has enough free margin to open a position
    bool check_margin(double required_margin) const {
        return free_margin_ >= required_margin;
    }

    // Update account equity, margin, free margin from active positions
    void update_state(const std::vector<PositionEntity>& active_positions, const SymbolSpec& sym) {
        double floating_pnl = 0.0;
        double total_margin = 0.0;

        double long_lots = 0.0;
        double short_lots = 0.0;
        double avg_long_price = 0.0;
        double avg_short_price = 0.0;

        for (const auto& pos : active_positions) {
            floating_pnl += pos.profit + pos.swap;
            if (pos.is_buy()) {
                long_lots += pos.volume;
                avg_long_price = pos.price_open;
            } else {
                short_lots += pos.volume;
                avg_short_price = pos.price_open;
            }
        }

        if (config_.margin_mode == ACCOUNT_MARGIN_MODE_RETAIL_HEDGING) {
            // In hedging, margin is required for both sides (or max side if hedged margin is 0)
            // Standard MT5 default: required margin per position
            for (const auto& pos : active_positions) {
                total_margin += calculate_required_margin(sym, pos.volume, pos.price_open);
            }
        } else {
            // Netting mode: net volume
            double net_lots = std::abs(long_lots - short_lots);
            double ref_price = (long_lots >= short_lots) ? avg_long_price : avg_short_price;
            total_margin = calculate_required_margin(sym, net_lots, ref_price);
        }

        margin_      = total_margin;
        equity_      = balance_ + floating_pnl;
        free_margin_ = equity_ - margin_;
        _recalculate_margin_level();

        // Check margin call
        if (margin_ > 0.0 && margin_level_ <= config_.margin_call_level) {
            margin_call_count_++;
        }
    }

    // Check if stop-out condition is reached
    bool is_stopout() const {
        if (margin_ <= EPSILON_PRICE) return false;
        return margin_level_ <= config_.margin_stopout_level;
    }

    void record_stopout() {
        stopout_count_++;
    }

    // MQL5 AccountInfoDouble equivalent
    double info_double(ENUM_ACCOUNT_INFO_DOUBLE prop) const {
        switch (prop) {
            case ACCOUNT_BALANCE:        return balance_;
            case ACCOUNT_EQUITY:         return equity_;
            case ACCOUNT_MARGIN:         return margin_;
            case ACCOUNT_MARGIN_FREE:    return free_margin_;
            case ACCOUNT_MARGIN_LEVEL:   return margin_level_;
            case ACCOUNT_MARGIN_SO_CALL: return config_.margin_call_level;
            case ACCOUNT_MARGIN_SO_SO:   return config_.margin_stopout_level;
            case ACCOUNT_PROFIT:         return equity_ - balance_;
            default:                     return 0.0;
        }
    }

    // MQL5 AccountInfoInteger equivalent
    int64_t info_integer(ENUM_ACCOUNT_INFO_INTEGER prop) const {
        switch (prop) {
            case ACCOUNT_LEVERAGE:    return config_.leverage;
            case ACCOUNT_MARGIN_MODE: return static_cast<int64_t>(config_.margin_mode);
            case ACCOUNT_TRADE_MODE:  return 0; // Demo/test
            default:                  return 0;
        }
    }

private:
    void _recalculate_margin_level() {
        if (margin_ > EPSILON_PRICE) {
            margin_level_ = (equity_ / margin_) * 100.0;
        } else {
            margin_level_ = 0.0;
        }
    }

    AccountConfig config_;
    double  balance_;
    double  equity_;
    double  margin_;
    double  free_margin_;
    double  margin_level_;
    int32_t stopout_count_;
    int32_t margin_call_count_;
};

} // namespace edge
