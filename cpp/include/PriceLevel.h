#pragma once

#include "Order.h"

#include <cstdint>
#include <list>

// A single price level in the order book. Holds all resting orders at one
// specific price, in FIFO arrival order (time priority within a price level).

// Basically how it works is that the orderbook stores a queue of all price levels
// i.e 15032 , 15603, 15802 ...
// each price level stores for say price 15032 what is the order entry queue
// {ordere1,quantity1,time1}, {order2,q2,t2}.... 



struct PriceLevel {
    int64_t price;                 
    std::list<Order> orders;       // FIFO queue, oldest at front
    uint64_t total_quantity;       // cached sum of orders quantity

    explicit PriceLevel(int64_t p)
        : price(p), total_quantity(0) {}
};