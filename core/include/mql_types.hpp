/**
 * EdgeEngine - Open-Source MT5 Strategy Tester Replica
 * mql_types.hpp - 1:1 definitions of MetaTrader 5 MQL5 data structures and constants.
 */
#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>

namespace edge {

// ============================================================
// Core Constants
// ============================================================
constexpr double EPSILON_PRICE = 1e-9;
constexpr double EMPTY_VALUE   = std::numeric_limits<double>::quiet_NaN();

// ============================================================
// ENUM_ORDER_TYPE (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_ORDER_TYPE : int32_t {
    ORDER_TYPE_BUY             = 0,  // Market Buy
    ORDER_TYPE_SELL            = 1,  // Market Sell
    ORDER_TYPE_BUY_LIMIT       = 2,  // Buy Limit pending
    ORDER_TYPE_SELL_LIMIT      = 3,  // Sell Limit pending
    ORDER_TYPE_BUY_STOP        = 4,  // Buy Stop pending
    ORDER_TYPE_SELL_STOP       = 5,  // Sell Stop pending
    ORDER_TYPE_BUY_STOP_LIMIT  = 6,  // Buy Stop Limit pending
    ORDER_TYPE_SELL_STOP_LIMIT = 7,  // Sell Stop Limit pending
    ORDER_TYPE_CLOSE_BY        = 8   // Close position by opposite position
};

// ============================================================
// ENUM_ORDER_STATE (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_ORDER_STATE : int32_t {
    ORDER_STATE_STARTED  = 0,  // Order checked, but not yet accepted
    ORDER_STATE_PLACED   = 1,  // Order accepted and placed in book
    ORDER_STATE_CANCELED = 2,  // Order canceled by client/EA
    ORDER_STATE_PARTIAL  = 3,  // Order partially executed
    ORDER_STATE_FILLED   = 4,  // Order fully executed
    ORDER_STATE_REJECTED = 5,  // Order rejected by dealer/engine
    ORDER_STATE_EXPIRED  = 6,  // Order expired
    ORDER_STATE_REQUEST_ADD    = 7,
    ORDER_STATE_REQUEST_MODIFY = 8,
    ORDER_STATE_REQUEST_CANCEL = 9
};

// ============================================================
// ENUM_ORDER_TYPE_FILLING (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_ORDER_TYPE_FILLING : int32_t {
    ORDER_FILLING_FOK    = 0,  // Fill or Kill
    ORDER_FILLING_IOC    = 1,  // Immediate or Cancel
    ORDER_FILLING_RETURN = 2   // Return remaining volume
};

// ============================================================
// ENUM_ORDER_TYPE_TIME (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_ORDER_TYPE_TIME : int32_t {
    ORDER_TIME_GTC           = 0,  // Good till cancel
    ORDER_TIME_DAY           = 1,  // Good till end of current day
    ORDER_TIME_SPECIFIED     = 2,  // Good till specified timestamp
    ORDER_TIME_SPECIFIED_DAY = 3   // Good till specified date
};

// ============================================================
// ENUM_POSITION_TYPE (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_POSITION_TYPE : int32_t {
    POSITION_TYPE_BUY  = 0,
    POSITION_TYPE_SELL = 1
};

// ============================================================
// ENUM_DEAL_TYPE (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_DEAL_TYPE : int32_t {
    DEAL_TYPE_BUY                      = 0,
    DEAL_TYPE_SELL                     = 1,
    DEAL_TYPE_BALANCE                  = 2,
    DEAL_TYPE_CREDIT                   = 3,
    DEAL_TYPE_CHARGE                   = 4,
    DEAL_TYPE_CORRECTION               = 5,
    DEAL_TYPE_BONUS                    = 6,
    DEAL_TYPE_COMMISSION               = 7,
    DEAL_TYPE_COMMISSION_DAILY         = 8,
    DEAL_TYPE_COMMISSION_MONTHLY       = 9,
    DEAL_TYPE_COMMISSION_AGENT_DAILY   = 10,
    DEAL_TYPE_COMMISSION_AGENT_MONTHLY = 11,
    DEAL_TYPE_INTEREST                 = 12,
    DEAL_TYPE_BUY_CANCELED             = 13,
    DEAL_TYPE_SELL_CANCELED            = 14,
    DEAL_DIVIDEND                      = 15,
    DEAL_DIVIDEND_FRANKED              = 16,
    DEAL_TAX                           = 17
};

// ============================================================
// ENUM_DEAL_ENTRY (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_DEAL_ENTRY : int32_t {
    DEAL_ENTRY_IN      = 0,  // Entry into market
    DEAL_ENTRY_OUT     = 1,  // Exit from market
    DEAL_ENTRY_INOUT   = 2,  // Reverse position
    DEAL_ENTRY_OUT_BY  = 3   // Close by opposite position
};

// ============================================================
// ENUM_DEAL_REASON (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_DEAL_REASON : int32_t {
    DEAL_REASON_CLIENT   = 0,  // Manual or EA client request
    DEAL_REASON_MOBILE   = 1,
    DEAL_REASON_WEB      = 2,
    DEAL_REASON_EXPERT   = 3,  // From EA
    DEAL_REASON_SL       = 4,  // Triggered by Stop Loss
    DEAL_REASON_TP       = 5,  // Triggered by Take Profit
    DEAL_REASON_SO       = 6,  // Triggered by Stop Out
    DEAL_REASON_ROLLOVER = 7
};

// ============================================================
// ENUM_TRADE_TRANSACTION_TYPE (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_TRADE_TRANSACTION_TYPE : int32_t {
    TRADE_TRANSACTION_ORDER_ADD    = 0,
    TRADE_TRANSACTION_ORDER_UPDATE = 1,
    TRADE_TRANSACTION_ORDER_DELETE = 2,
    TRADE_TRANSACTION_DEAL_ADD     = 3,
    TRADE_TRANSACTION_DEAL_UPDATE  = 4,
    TRADE_TRANSACTION_DEAL_DELETE  = 5,
    TRADE_TRANSACTION_HISTORY_ADD  = 6,
    TRADE_TRANSACTION_HISTORY_UPDATE= 7,
    TRADE_TRANSACTION_HISTORY_DELETE= 8,
    TRADE_TRANSACTION_POSITION     = 9,
    TRADE_TRANSACTION_REQUEST      = 10
};

// ============================================================
// ENUM_TRADE_REQUEST_ACTIONS (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_TRADE_REQUEST_ACTIONS : int32_t {
    TRADE_ACTION_DEAL     = 1,  // Place market order for instant execution
    TRADE_ACTION_PENDING  = 5,  // Place pending order
    TRADE_ACTION_SLTP     = 6,  // Modify Stop Loss and Take Profit
    TRADE_ACTION_MODIFY   = 7,  // Modify pending order parameters
    TRADE_ACTION_REMOVE   = 8,  // Delete pending order
    TRADE_ACTION_CLOSE_BY = 10  // Close position by opposite
};

// ============================================================
// Trade Return Codes (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_TRADE_RETCODE : uint32_t {
    TRADE_RETCODE_REQUOTE            = 10004,
    TRADE_RETCODE_REJECT             = 10006,
    TRADE_RETCODE_CANCEL             = 10007,
    TRADE_RETCODE_PLACED             = 10008,
    TRADE_RETCODE_DONE               = 10009,
    TRADE_RETCODE_DONE_PARTIAL       = 10010,
    TRADE_RETCODE_ERROR              = 10011,
    TRADE_RETCODE_TIMEOUT            = 10012,
    TRADE_RETCODE_INVALID            = 10013,
    TRADE_RETCODE_INVALID_VOLUME     = 10014,
    TRADE_RETCODE_INVALID_PRICE      = 10015,
    TRADE_RETCODE_INVALID_STOPS      = 10016,
    TRADE_RETCODE_TRADE_DISABLED     = 10017,
    TRADE_RETCODE_MARKET_CLOSED      = 10018,
    TRADE_RETCODE_NO_MONEY           = 10019,
    TRADE_RETCODE_PRICE_CHANGED      = 10020,
    TRADE_RETCODE_PRICE_OFF          = 10021,
    TRADE_RETCODE_INVALID_EXPIRATION = 10022,
    TRADE_RETCODE_ORDER_CHANGED      = 10023,
    TRADE_RETCODE_TOO_MANY_REQUESTS  = 10024,
    TRADE_RETCODE_NO_CHANGES         = 10025,
    TRADE_RETCODE_SERVER_DISABLES_AT = 10026,
    TRADE_RETCODE_CLIENT_DISABLES_AT = 10027,
    TRADE_RETCODE_LOCKED             = 10028,
    TRADE_RETCODE_FROZEN             = 10029,
    TRADE_RETCODE_INVALID_FILL       = 10030,
    TRADE_RETCODE_CONNECTION         = 10031,
    TRADE_RETCODE_ONLY_REAL          = 10032,
    TRADE_RETCODE_LIMIT_ORDERS       = 10033,
    TRADE_RETCODE_LIMIT_VOLUME       = 10034,
    TRADE_RETCODE_POSITION_CLOSED    = 10035,
    TRADE_RETCODE_INVALID_CLOSE_VOLUME= 10036,
    TRADE_RETCODE_CLOSE_ORDER_EXIST  = 10037,
    TRADE_RETCODE_LIMIT_POSITIONS    = 10038,
    TRADE_RETCODE_REJECT_CANCEL      = 10039,
    TRADE_RETCODE_LONG_ONLY          = 10040,
    TRADE_RETCODE_SHORT_ONLY         = 10041,
    TRADE_RETCODE_CLOSE_ONLY         = 10042
};

// ============================================================
// ENUM_ACCOUNT_MARGIN_MODE (1:1 with MT5 MQL5)
// ============================================================
enum ENUM_ACCOUNT_MARGIN_MODE : int32_t {
    ACCOUNT_MARGIN_MODE_RETAIL_NETTING = 0,
    ACCOUNT_MARGIN_MODE_EXCHANGE       = 1,
    ACCOUNT_MARGIN_MODE_RETAIL_HEDGING = 2
};

// ============================================================
// ENUM_ACCOUNT_INFO_DOUBLE
// ============================================================
enum ENUM_ACCOUNT_INFO_DOUBLE : int32_t {
    ACCOUNT_BALANCE        = 0,
    ACCOUNT_CREDIT         = 1,
    ACCOUNT_PROFIT         = 2,
    ACCOUNT_EQUITY         = 3,
    ACCOUNT_MARGIN         = 4,
    ACCOUNT_MARGIN_FREE    = 5,
    ACCOUNT_MARGIN_LEVEL   = 6,
    ACCOUNT_MARGIN_SO_CALL = 7,
    ACCOUNT_MARGIN_SO_SO   = 8
};

// ============================================================
// ENUM_ACCOUNT_INFO_INTEGER
// ============================================================
enum ENUM_ACCOUNT_INFO_INTEGER : int32_t {
    ACCOUNT_LOGIN       = 0,
    ACCOUNT_TRADE_MODE  = 1,
    ACCOUNT_LEVERAGE    = 2,
    ACCOUNT_MARGIN_MODE = 3
};

// ============================================================
// MqlTick (1:1 with MT5 MQL5)
// ============================================================
struct MqlTick {
    int64_t  time      = 0;      // Unix seconds
    double   bid       = 0.0;    // Current Bid price
    double   ask       = 0.0;    // Current Ask price
    double   last      = 0.0;    // Price of the last deal
    uint64_t volume    = 0;      // Volume for the current Last price
    int64_t  time_msc  = 0;      // Time in milliseconds
    uint32_t flags     = 0;      // Tick flags (TICK_FLAG_BID, etc.)
    double   volume_real = 0.0;  // Volume for current Last price with higher precision
};

// ============================================================
// MqlRates (1:1 with MT5 MQL5)
// ============================================================
struct MqlRates {
    int64_t  time        = 0;
    double   open        = 0.0;
    double   high        = 0.0;
    double   low         = 0.0;
    double   close       = 0.0;
    int64_t  tick_volume = 0;
    int32_t  spread      = 0;
    int64_t  real_volume = 0;
};

// Backward-compatible alias
using Bar = MqlRates;

// ============================================================
// MqlTradeRequest (1:1 with MT5 MQL5)
// ============================================================
struct MqlTradeRequest {
    ENUM_TRADE_REQUEST_ACTIONS action      = TRADE_ACTION_DEAL;
    uint64_t                   magic       = 0;
    uint64_t                   order       = 0;
    std::string                symbol      = "";
    double                     volume      = 0.0;
    double                     price       = 0.0;
    double                     stoplimit   = 0.0;
    double                     sl          = 0.0;
    double                     tp          = 0.0;
    uint64_t                   deviation   = 0;
    ENUM_ORDER_TYPE            type        = ORDER_TYPE_BUY;
    ENUM_ORDER_TYPE_FILLING    type_filling= ORDER_FILLING_FOK;
    ENUM_ORDER_TYPE_TIME       type_time   = ORDER_TIME_GTC;
    int64_t                    expiration  = 0;
    std::string                comment     = "";
    uint64_t                   position    = 0;
    uint64_t                   position_by = 0;
};

// ============================================================
// MqlTradeResult (1:1 with MT5 MQL5)
// ============================================================
struct MqlTradeResult {
    uint32_t    retcode    = TRADE_RETCODE_DONE;
    uint64_t    deal       = 0;
    uint64_t    order      = 0;
    double      volume     = 0.0;
    double      price      = 0.0;
    double      bid        = 0.0;
    double      ask        = 0.0;
    std::string comment    = "";
    uint32_t    request_id = 0;
    uint32_t    retcode_external = 0;
};

// ============================================================
// MqlTradeCheckResult (1:1 with MT5 MQL5)
// ============================================================
struct MqlTradeCheckResult {
    uint32_t    retcode       = TRADE_RETCODE_DONE;
    double      balance       = 0.0;
    double      equity        = 0.0;
    double      profit        = 0.0;
    double      margin        = 0.0;
    double      margin_free   = 0.0;
    double      margin_level  = 0.0;
    std::string comment       = "";
};

// ============================================================
// MqlTradeTransaction (1:1 with MT5 MQL5)
// ============================================================
struct MqlTradeTransaction {
    uint64_t                    deal          = 0;
    uint64_t                    order         = 0;
    std::string                 symbol        = "";
    ENUM_TRADE_TRANSACTION_TYPE type          = TRADE_TRANSACTION_REQUEST;
    ENUM_ORDER_TYPE             order_type    = ORDER_TYPE_BUY;
    ENUM_ORDER_STATE            order_state   = ORDER_STATE_STARTED;
    ENUM_DEAL_TYPE              deal_type     = DEAL_TYPE_BUY;
    ENUM_ORDER_TYPE_TIME        time_type     = ORDER_TIME_GTC;
    int64_t                     time_expiration = 0;
    double                      price         = 0.0;
    double                      price_trigger = 0.0;
    double                      price_sl      = 0.0;
    double                      price_tp      = 0.0;
    double                      volume        = 0.0;
    uint64_t                    position      = 0;
    uint64_t                    position_by   = 0;
};

// ============================================================
// Order Entity (Internal active order in tester engine)
// ============================================================
struct Order {
    uint64_t                ticket       = 0;
    int64_t                 time_setup   = 0;
    ENUM_ORDER_TYPE         type         = ORDER_TYPE_BUY;
    ENUM_ORDER_STATE        state        = ORDER_STATE_PLACED;
    ENUM_ORDER_TYPE_TIME    time_type    = ORDER_TIME_GTC;
    int64_t                 time_expiration = 0;
    double                  price_open   = 0.0;
    double                  price_current= 0.0;
    double                  price_stoplimit = 0.0;
    double                  sl           = 0.0;
    double                  tp           = 0.0;
    double                  volume_initial = 0.0;
    double                  volume_current = 0.0;
    std::string             symbol       = "";
    std::string             comment      = "";
    uint64_t                magic        = 0;
    uint64_t                position_id  = 0;
};

// ============================================================
// Position Entity (Internal active position in tester engine)
// ============================================================
struct PositionEntity {
    uint64_t            ticket        = 0;
    int64_t             time          = 0;
    int64_t             time_msc      = 0;
    int64_t             time_update   = 0;
    ENUM_POSITION_TYPE  type          = POSITION_TYPE_BUY;
    uint64_t            magic         = 0;
    uint64_t            identifier    = 0;
    double              volume        = 0.0;
    double              price_open    = 0.0;
    double              price_current = 0.0;
    double              sl            = 0.0;
    double              tp            = 0.0;
    double              profit        = 0.0;
    double              swap          = 0.0;
    std::string         symbol        = "";
    std::string         comment       = "";

    // Simulation tracking
    int32_t             entry_bar     = 0;
    double              mfe           = 0.0;
    double              mae           = 0.0;
    double              highest_price = 0.0;
    double              lowest_price  = 0.0;

    bool is_buy() const  { return type == POSITION_TYPE_BUY; }
    bool is_sell() const { return type == POSITION_TYPE_SELL; }
};

// ============================================================
// Deal Entity (Internal executed deal in tester engine)
// ============================================================
struct Deal {
    uint64_t         ticket       = 0;
    uint64_t         order        = 0;
    int64_t          time         = 0;
    int64_t          time_msc     = 0;
    ENUM_DEAL_TYPE   type         = DEAL_TYPE_BUY;
    ENUM_DEAL_ENTRY  entry        = DEAL_ENTRY_IN;
    uint64_t         magic        = 0;
    ENUM_DEAL_REASON reason       = DEAL_REASON_EXPERT;
    uint64_t         position_id  = 0;
    double           volume       = 0.0;
    double           price        = 0.0;
    double           commission   = 0.0;
    double           swap         = 0.0;
    double           profit       = 0.0;
    double           fee          = 0.0;
    double           sl           = 0.0;
    double           tp           = 0.0;
    std::string      symbol       = "";
    std::string      comment      = "";
};

} // namespace edge
