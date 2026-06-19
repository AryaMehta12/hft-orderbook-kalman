#include "OrderBook.h"

#include <cassert>
#include <iostream>

OrderBook::OrderBook()
    : next_order_id_(1)
    , verbose_(false)
{}

uint64_t OrderBook::submitLimitOrder(Order order) {
    assert(order.id == 0);
    order.id = next_order_id_++;
    last_trades_.clear();

    const uint64_t initial_qty = order.quantity;
    const uint64_t remaining = matchOrder(order, /*stop_on_price=*/true);
    order.quantity = remaining;

    if (remaining > 0) {
        restLimitOrder(order);
    }

    if (verbose_) {
        std::cout << "LIMIT  "
                  << (order.side == Side::BUY ? "BUY " : "SELL")
                  << " id=" << order.id
                  << " price=" << order.price
                  << " filled=" << (initial_qty - remaining)
                  << " rested=" << remaining
                  << "\n";
    }

    return order.id;
}

uint64_t OrderBook::submitMarketOrder(Order order) {
    assert(order.id == 0);
    order.id = next_order_id_++;
    last_trades_.clear();

    const uint64_t initial_qty = order.quantity;
    const uint64_t remaining = matchOrder(order, /*stop_on_price=*/false);
    // Market orders do NOT rest. Unfilled remainder is discarded (IOC).

    if (verbose_) {
        std::cout << "MARKET "
                  << (order.side == Side::BUY ? "BUY " : "SELL")
                  << " id=" << order.id
                  << " filled=" << (initial_qty - remaining)
                  << " discarded=" << remaining
                  << "\n";
    }

    return order.id;
}

bool OrderBook::cancelOrder(uint64_t order_id) {
    auto it = order_index_.find(order_id);
    if (it == order_index_.end()) {
        if (verbose_) std::cout << "CANCEL id=" << order_id << " (not found)\n";
        return false;
    }
    removeOrderAt(it->second);
    if (verbose_) std::cout << "CANCEL id=" << order_id << " (ok)\n";
    return true;
}

int64_t OrderBook::bestBid() const {
    return bids_.empty() ? 0 : bids_.begin()->first;
}

int64_t OrderBook::bestAsk() const {
    return asks_.empty() ? 0 : asks_.begin()->first;
}

int64_t OrderBook::midPrice() const {
    const int64_t bid = bestBid();
    const int64_t ask = bestAsk();
    if (bid == 0 || ask == 0) return 0;
    return (bid + ask) / 2;
}

// ---------------------------------------------------------------------------
// matchOrder — core matching loop.
//
// Walks the opposite side from best price outward, consuming liquidity until
// either (a) the incoming order is fully filled or (b) the opposite side has
// no more crossable liquidity. Produces one Trade per fill, pushed into
// last_trades_. Mutates incoming.quantity as fills happen. Returns the
// unfilled quantity remaining.
//
// stop_on_price=true  → LIMIT semantics (stop when opposite price no longer
//                       crosses incoming.price)
// stop_on_price=false → MARKET semantics (stop only when opposite side empty)
//
// Trades execute at the RESTING order's price (price improvement for the
// aggressive party). This is standard exchange behavior.
// ---------------------------------------------------------------------------
uint64_t OrderBook::matchOrder(Order& incoming, bool stop_on_price) {
    if (incoming.side == Side::BUY) {
        // BUY matches against asks_ (lowest ask = best, at begin()).
        while (incoming.quantity > 0 && !asks_.empty()) {
            auto level_it = asks_.begin();
            const int64_t level_price = level_it->first;

            // Limit semantics: stop if best ask no longer crosses incoming.price.
            // For a BUY limit at price P, asks crossing means ask.price <= P.
            if (stop_on_price && level_price > incoming.price) break;

            PriceLevel& level = level_it->second;

            // Walk this level's FIFO queue (oldest at front).
            while (incoming.quantity > 0 && !level.orders.empty()) {
                Order& resting = level.orders.front();

                if (incoming.quantity >= resting.quantity) {
                    // Full consumption of the resting order.
                    const uint64_t fill_qty = resting.quantity;
                    const uint64_t resting_id = resting.id;
                    Trade t{};
                    t.aggressiveId = incoming.id;
                    t.passiveId    = resting_id;       // or resting.id
                    t.price         = level_price;
                    t.quantity      = fill_qty;
                    t.timestamp     = incoming.timestamp;
                    last_trades_.push_back(t);

                    incoming.quantity -= fill_qty;

                    // Build locator and remove. removeOrderAt may erase the
                    // level itself from asks_ if this was the last order at
                    // this price — that's why we cache level_price above and
                    // don't touch 'level' after this call.
                    OrderLocator loc{Side::SELL, level_price, level.orders.begin()};
                    removeOrderAt(loc);

                    // If the level was just erased, break out of the inner
                    // loop. The outer loop will re-check asks_.empty() and
                    // fetch a fresh begin() if liquidity remains.
                    if (asks_.find(level_price) == asks_.end()) break;
                } else {
                    // Partial consumption of the resting order.
                    // The resting order stays in the book with reduced qty.
                    const uint64_t fill_qty = incoming.quantity;

                    Trade t{};
                    t.aggressiveId = incoming.id;
                    t.passiveId    = resting.id;       // or resting.id
                    t.price         = level_price;
                    t.quantity      = fill_qty;
                    t.timestamp     = incoming.timestamp;
                    last_trades_.push_back(t);

                    resting.quantity -= fill_qty;
                    level.total_quantity -= fill_qty;
                    incoming.quantity = 0;
                    // Inner and outer loop conditions will both exit naturally.
                }
            }
        }
    } else {
        // SELL matches against bids_ (highest bid = best, at begin()).
        while (incoming.quantity > 0 && !bids_.empty()) {
            auto level_it = bids_.begin();
            const int64_t level_price = level_it->first;

            // Limit semantics: stop if best bid no longer crosses incoming.price.
            // For a SELL limit at price P, bids crossing means bid.price >= P.
            if (stop_on_price && level_price < incoming.price) break;

            PriceLevel& level = level_it->second;

            while (incoming.quantity > 0 && !level.orders.empty()) {
                Order& resting = level.orders.front();

                if (incoming.quantity >= resting.quantity) {
                    const uint64_t fill_qty = resting.quantity;
                    const uint64_t resting_id = resting.id;

                    Trade t{};
                    t.aggressiveId = incoming.id;
                    t.passiveId    = resting_id;       // or resting.id
                    t.price         = level_price;
                    t.quantity      = fill_qty;
                    t.timestamp     = incoming.timestamp;
                    last_trades_.push_back(t);
                    incoming.quantity -= fill_qty;

                    OrderLocator loc{Side::BUY, level_price, level.orders.begin()};
                    removeOrderAt(loc);

                    if (bids_.find(level_price) == bids_.end()) break;
                } else {
                    const uint64_t fill_qty = incoming.quantity;

                    Trade t{};
                    t.aggressiveId = incoming.id;
                    t.passiveId    = resting.id;       // or resting.id
                    t.price         = level_price;
                    t.quantity      = fill_qty;
                    t.timestamp     = incoming.timestamp;
                    last_trades_.push_back(t);
                    resting.quantity -= fill_qty;
                    level.total_quantity -= fill_qty;
                    incoming.quantity = 0;
                }
            }
        }
    }

    return incoming.quantity;
}

void OrderBook::restLimitOrder(const Order& order) {
    if (order.side == Side::BUY) {
        auto map_it = bids_.try_emplace(order.price, order.price).first;
        // How try emplace works is , try_emplace(key,arg1,arg2..)
        // it checks for presence of key in map
        // if present just return the iterator, if not pass the args to the constructor of the value, and then return the iterator!
        PriceLevel& level = map_it->second;
        auto list_it = level.orders.insert(level.orders.end(), order);
        level.total_quantity += order.quantity;
        order_index_[order.id] = OrderLocator{Side::BUY, order.price, list_it};
    } else {
        auto map_it = asks_.try_emplace(order.price, order.price).first; 
        PriceLevel& level = map_it->second;
        auto list_it = level.orders.insert(level.orders.end(), order);
        level.total_quantity += order.quantity;
        order_index_[order.id] = OrderLocator{Side::SELL, order.price, list_it};
    }
}

void OrderBook::removeOrderAt(const OrderLocator& loc) {
    // Extract everything we need from loc BEFORE mutating order_index_,
    // because loc is a reference into order_index_ and erase invalidates it.
    const Side side = loc.side;
    const int64_t price = loc.price;
    const auto list_it = loc.it;
    const uint64_t order_id = list_it->id;
    const uint64_t qty = list_it->quantity;

    if (side == Side::BUY) {
        auto map_it = bids_.find(price);
        assert(map_it != bids_.end());
        PriceLevel& level = map_it->second;
        level.orders.erase(list_it);
        level.total_quantity -= qty;
        if (level.orders.empty()) bids_.erase(map_it);
    } else {
        auto map_it = asks_.find(price);
        assert(map_it != asks_.end());
        PriceLevel& level = map_it->second;
        level.orders.erase(list_it);
        level.total_quantity -= qty;
        if (level.orders.empty()) asks_.erase(map_it);
    }

    order_index_.erase(order_id);
}