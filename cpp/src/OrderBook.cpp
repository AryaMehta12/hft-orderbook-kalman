// ===========================================================================
// FILE 1 of 3 — save as: cpp/src/OrderBook.cpp
// ===========================================================================

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
// matchOrder — STUB for Block 2.
// ---------------------------------------------------------------------------
uint64_t OrderBook::matchOrder(Order& incoming, bool stop_on_price) {
    (void)stop_on_price;
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