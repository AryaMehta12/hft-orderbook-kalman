#pragma once

#include "Order.h"
#include "PriceLevel.h"

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// OrderBook
// ---------------------------------------------------------------------------
// Single-symbol limit order book with price-time priority matching.
//
// Two sides:
//   - bids: sorted DESCENDING by price (best bid = highest = begin())
//   - asks: sorted ASCENDING  by price (best ask = lowest  = begin())
// Each side maps price -> PriceLevel; within a level, orders are FIFO
// (time priority).
//
// Two public entry points for new orders:
//   - submitLimitOrder:  matches against opposite side if marketable;
//                        rests any unfilled remainder in the book.
//   - submitMarketOrder: matches against opposite side only;
//                        unfilled remainder is discarded (IOC semantics).
//                        The Order's price field is ignored.
//
// Cancels are O(1) via an order-id -> (side, price, list iterator) lookup
// table. Stable iterators are why PriceLevel::orders is std::list — see
// PriceLevel.h for the rationale.
class OrderBook {
public:
    OrderBook();

    // Submit a LIMIT order. Returns the engine-assigned order ID.
    // Precondition: order.id == 0 on entry. The engine assigns a fresh
    // monotonic id and returns it.
    uint64_t submitLimitOrder(Order order);

    // Submit a MARKET order. Returns the engine-assigned order ID.
    // The order.price field is ignored.
    // Precondition: order.id == 0 on entry.
    uint64_t submitMarketOrder(Order order);

    // Cancel a resting order by ID. Returns true if cancelled, false if the
    // order ID is unknown (already filled, already cancelled, or never existed).
    bool cancelOrder(uint64_t order_id);

    // Top-of-book queries. Return 0 if the side is empty.
    int64_t bestBid() const;
    int64_t bestAsk() const;

    // Mid-price = (bestBid + bestAsk) / 2 when both sides are populated.
    // Returns 0 if either side is empty; caller must treat 0 as "no mid".
    int64_t midPrice() const;

    // Trades produced by the most recent submit*Order call.
    // Caller reads this immediately after submission; it is overwritten
    // on the next call.
    const std::vector<Trade>& lastTrades() const { return last_trades_; }

    // Total resting orders across both sides. O(1).
    size_t restingOrderCount() const { return order_index_.size(); }

    bool isResting(uint64_t order_id) const{
        return order_index_.find(order_id)!=order_index_.end();
    }

    // Toggle per-event console output for debugging. Off by default.
    void setVerbose(bool v) { verbose_ = v; }

private:
    // Bids: best (highest) price at begin().
    using BidMap = std::map<int64_t, PriceLevel, std::greater<int64_t>>;
    // Asks: best (lowest) price at begin().
    using AskMap = std::map<int64_t, PriceLevel, std::less<int64_t>>;

    // For cancel-by-ID: locate the side, the price level, and the exact
    // list node within that level's FIFO queue.
    struct OrderLocator {
        Side side;
        int64_t price;
        std::list<Order>::iterator it;
    };

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<uint64_t, OrderLocator> order_index_;

    uint64_t next_order_id_;
    std::vector<Trade> last_trades_;
    bool verbose_;

    // Shared matching helper.
    //   stop_on_price=true  → LIMIT semantics (stop when opposite side
    //                         no longer crosses incoming.price)
    //   stop_on_price=false → MARKET semantics (stop only when opposite
    //                         side is empty) , buy off everything
    // Returns the unfilled quantity remaining on the incoming order.
    // WRITTEN BY THE USER in Block 3 (see CONTEXT.md Rule 3).
    uint64_t matchOrder(Order& incoming, bool stop_on_price);

    // Insert a resting limit order into the appropriate side.
    void restLimitOrder(const Order& order); // passive orders

    // Remove a single order from a price level given its locator.
    // If the level becomes empty, the level itself is erased from its map.
    void removeOrderAt(const OrderLocator& loc);
};