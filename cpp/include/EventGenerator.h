#pragma once

#include "Order.h"

#include <cstdint>
#include <random>

// ---------------------------------------------------------------------------
// Event
// ---------------------------------------------------------------------------
// One unit of activity the order book consumes. There are three event kinds.
// We use a tagged-union style: the 'type' field tells you which fields are
// valid for this event.
//
//   ADD_LIMIT  : order field is meaningful (side, price, quantity, timestamp)
//   ADD_MARKET : order field is meaningful (side, quantity, timestamp; price ignored)
//   CANCEL     : cancel_id field is meaningful; order field ignored
// ---------------------------------------------------------------------------
enum class EventType {
    ADD_LIMIT,
    ADD_MARKET,
    CANCEL
};

struct Event {
    EventType type;
    Order     order;       // valid for ADD_LIMIT and ADD_MARKET
    uint64_t  cancel_id;   // valid for CANCEL (an order id previously assigned)
    uint64_t  timestamp;   // simulated nanos since simulation start
};

// ---------------------------------------------------------------------------
// EventGenerator
// ---------------------------------------------------------------------------
// Produces a microstructurally-realistic synthetic event stream.
//
//   True price: slow Brownian motion (small Gaussian steps), starting at the
//               configured initial price. Updated on every event.
//
//   Event mix: 52% ADD_LIMIT / 40% CANCEL / 8% ADD_MARKET, calibrated to
//              roughly match real exchange order-flow ratios.
//
//   Limit prices: Gaussian-centered on the true price with σ ≈ 10 ticks,
//                 so most limits land near the touch, with thinning depth.
//                 BUY limits get priced ≤ true_price; SELL limits ≥ true_price.
//                 (Limits that cross would just trade immediately; we want
//                  resting depth, so we bias them to the passive side.)
//
//   Quantities: small integers with skew toward small sizes. Exponential
//               distribution clipped to [1, 500].
//
//   Cancels: pick a random resting order id from those we've tracked. If
//            there are no resting orders, we re-roll to LIMIT instead.
//
//   Timestamps: each event happens after a random gap drawn from an
//               exponential distribution (Poisson arrival process), so the
//               stream has realistically irregular inter-arrival times.
//
// The generator owns the RNG. The seed is fixed by default for reproducibility,
// overridable via the constructor.
// ---------------------------------------------------------------------------
class EventGenerator {
public:
    // Construct with seed and initial true price (in ticks).
    EventGenerator(uint64_t seed, int64_t initial_true_price);

    // Produce the next event. Each call advances the simulation by one event.
    Event next();

    // The current true (hidden) price, for ground-truth comparison later.
    int64_t trueprice() const { return true_price_; }

    // Tell the generator that an order id is now resting (so it becomes a
    // candidate for future cancels). Called by main.cpp after submitting a
    // LIMIT order that didn't fully fill.
    void registerResting(uint64_t order_id);

    // Tell the generator an id is no longer resting (filled or cancelled).
    void unregisterResting(uint64_t order_id);

private:
    // RNG and distributions.
    std::mt19937_64                              rng_;
    std::normal_distribution<double>             true_price_step_;   // Brownian step
    std::normal_distribution<double>             limit_offset_;      // limit price offset from true
    std::exponential_distribution<double>        quantity_dist_;     // size skew
    std::exponential_distribution<double>        arrival_gap_dist_;  // inter-arrival ns
    std::uniform_real_distribution<double>       event_kind_dist_;   // for type mix
    std::bernoulli_distribution                  side_dist_;         // 50/50 buy/sell

    int64_t  true_price_;
    uint64_t now_ns_;       // current simulated time, accumulated from arrivals

    // Resting order ids we can cancel. Cheap "set" using a vector + swap-remove.
    std::vector<uint64_t> resting_ids_;

    // Internal helpers.
    uint64_t drawQuantity();
    int64_t  drawLimitPrice(Side side);
    void     stepTruePrice();
    void     advanceTime();
};