#include "EventGenerator.h"

#include <algorithm>
#include <cmath>

EventGenerator::EventGenerator(uint64_t seed, int64_t initial_true_price)
    : rng_(seed)
    , true_price_step_(0.0, 1.0)       // mean 0, σ 1 tick per step
    , limit_offset_(0.0, 10.0)         // mean 0, σ 10 ticks
    , quantity_dist_(0.02)             // exp rate, mean ≈ 50 shares
    , arrival_gap_dist_(1.0 / 1000.0)  // mean inter-arrival 1000 ns = 1 µs
    , event_kind_dist_(0.0, 1.0)
    , side_dist_(0.5)
    , true_price_(initial_true_price)
    , now_ns_(0)
{
    resting_ids_.reserve(10000);
}

void EventGenerator::registerResting(uint64_t order_id) {
    resting_ids_.push_back(order_id);
}

void EventGenerator::unregisterResting(uint64_t order_id) {
    // Swap-and-pop: find the id, swap it with the back, pop. O(n) worst case
    // but typical case fast because cancels often hit recently-added orders.
    for (size_t i = 0; i < resting_ids_.size(); ++i) {
        if (resting_ids_[i] == order_id) {
            resting_ids_[i] = resting_ids_.back();
            resting_ids_.pop_back();
            return;
        }
    }
    // If not found, the id was never registered or already removed — silently ignore.
}

uint64_t EventGenerator::drawQuantity() {
    double q = quantity_dist_(rng_);
    // Clip to [1, 500] and round to integer.
    int64_t qty = static_cast<int64_t>(std::round(q));
    if (qty < 1)   qty = 1;
    if (qty > 500) qty = 500;
    return static_cast<uint64_t>(qty);
}

int64_t EventGenerator::drawLimitPrice(Side side) {
    // Limit prices cluster around true_price_, with a Gaussian offset.
    // We bias to the passive side so most limits rest rather than cross:
    //   BUY  limits go BELOW true_price (passive bid)
    //   SELL limits go ABOVE true_price (passive ask)
    double offset = limit_offset_(rng_);
    double signed_offset = (side == Side::BUY) ? -std::abs(offset) : std::abs(offset);
    int64_t price = true_price_ + static_cast<int64_t>(std::round(signed_offset));
    return price;
}

void EventGenerator::stepTruePrice() {
    double step = true_price_step_(rng_);
    true_price_ += static_cast<int64_t>(std::round(step));
    // Keep true price positive — clamp at 1 tick if it ever drifts down too far.
    if (true_price_ < 1) true_price_ = 1;
}

void EventGenerator::advanceTime() {
    double gap_ns = arrival_gap_dist_(rng_);
    now_ns_ += static_cast<uint64_t>(std::max(1.0, std::round(gap_ns)));
}

Event EventGenerator::next() {
    advanceTime();
    stepTruePrice();

    double u = event_kind_dist_(rng_);

    Event ev{};
    ev.timestamp = now_ns_;

    // Determine event type. If we'd pick a CANCEL but there's nothing to
    // cancel, fall back to ADD_LIMIT instead.
    if (u < 0.52) {
        ev.type = EventType::ADD_LIMIT;
    } else if (u < 0.92) {
        ev.type = resting_ids_.empty() ? EventType::ADD_LIMIT : EventType::CANCEL;
    } else {
        ev.type = EventType::ADD_MARKET;
    }

    if (ev.type == EventType::CANCEL) {
        std::uniform_int_distribution<size_t> pick(0, resting_ids_.size() - 1);
        ev.cancel_id = resting_ids_[pick(rng_)];
    } else {
        Side s = side_dist_(rng_) ? Side::BUY : Side::SELL;
        ev.order.id        = 0;
        ev.order.side      = s;
        ev.order.quantity  = drawQuantity();
        ev.order.timestamp = now_ns_;
        if (ev.type == EventType::ADD_LIMIT) {
            ev.order.price = drawLimitPrice(s);
        } else {
            // ADD_MARKET: price ignored by the engine, set to 0 for clarity.
            ev.order.price = 0;
        }
    }

    return ev;
}