#include "OrderBook.h"

#include <cassert>
#include <cstdio>

// ---------------------------------------------------------------------------
// Helper: construct a non-crossing limit order with the required fields set.
// id is 0 (sentinel) so the engine assigns one; timestamp defaults to 0
// since these tests don't depend on time semantics.
// ---------------------------------------------------------------------------
static Order makeOrder(Side side, int64_t price, uint64_t qty, uint64_t ts = 0) {
    Order o{};
    o.id = 0;
    o.side = side;
    o.price = price;
    o.quantity = qty;
    o.timestamp = ts;
    return o;
}

// ---------------------------------------------------------------------------
// Test 1: a freshly-constructed book has zero state everywhere.
// ---------------------------------------------------------------------------
static void test_empty_book() {
    OrderBook book;
    assert(book.bestBid() == 0);
    assert(book.bestAsk() == 0);
    assert(book.midPrice() == 0);
    assert(book.restingOrderCount() == 0);
    assert(book.lastTrades().empty());
}

// ---------------------------------------------------------------------------
// Test 2: a single limit order rests correctly. ID assignment, resting path,
// top-of-book query.
// ---------------------------------------------------------------------------
static void test_add_single_limit() {
    OrderBook book;
    uint64_t id = book.submitLimitOrder(makeOrder(Side::BUY, 15000, 100));
    assert(id == 1);                    // first assigned id
    assert(book.bestBid() == 15000);
    assert(book.bestAsk() == 0);        // no asks present
    assert(book.restingOrderCount() == 1);
    assert(book.lastTrades().empty());  // matchOrder is a stub
}

// ---------------------------------------------------------------------------
// Test 3: two-sided book. Verifies the std::greater comparator on bids (best
// bid is highest), default std::less on asks (best ask is lowest), and the
// midPrice computation.
// ---------------------------------------------------------------------------
static void test_two_sides() {
    OrderBook book;
    book.submitLimitOrder(makeOrder(Side::BUY,  14900, 100));
    book.submitLimitOrder(makeOrder(Side::BUY,  15000, 200));  // better bid
    book.submitLimitOrder(makeOrder(Side::SELL, 15200, 150));
    book.submitLimitOrder(makeOrder(Side::SELL, 15100, 50));   // better ask

    assert(book.bestBid() == 15000);
    assert(book.bestAsk() == 15100);
    assert(book.midPrice() == 15050);
    assert(book.restingOrderCount() == 4);
}

// ---------------------------------------------------------------------------
// Test 4: cancel lifecycle. Cancel existing, demote best; cancel nonexistent;
// cancel already-cancelled; cancel last remaining; book becomes empty.
// ---------------------------------------------------------------------------
static void test_cancel() {
    OrderBook book;
    uint64_t id1 = book.submitLimitOrder(makeOrder(Side::BUY, 15000, 100));
    uint64_t id2 = book.submitLimitOrder(makeOrder(Side::BUY, 14900, 50));
    assert(book.restingOrderCount() == 2);

    assert(book.cancelOrder(id1) == true);
    assert(book.restingOrderCount() == 1);
    assert(book.bestBid() == 14900);    // best bid demoted

    assert(book.cancelOrder(9999) == false);   // unknown id
    assert(book.cancelOrder(id1)  == false);   // already cancelled

    assert(book.cancelOrder(id2) == true);
    assert(book.restingOrderCount() == 0);
    assert(book.bestBid() == 0);        // empty
}

// ---------------------------------------------------------------------------
// Test 5: multiple orders at the same price coexist (FIFO queue within a
// level). We can't observe FIFO order without matching, but count and best
// bid must be right.
// ---------------------------------------------------------------------------
static void test_fifo_within_level() {
    OrderBook book;
    book.submitLimitOrder(makeOrder(Side::BUY, 15000, 100, 1));
    book.submitLimitOrder(makeOrder(Side::BUY, 15000, 200, 2));
    book.submitLimitOrder(makeOrder(Side::BUY, 15000, 50,  3));

    assert(book.restingOrderCount() == 3);
    assert(book.bestBid() == 15000);
}

// ---------------------------------------------------------------------------
// Test 6: market order with the stub matchOrder. End-to-end code path runs;
// because matchOrder returns incoming.quantity unchanged, the market order
// fully "discards" and the book state is unchanged.
// ---------------------------------------------------------------------------
static void test_market_order_stub() {
    OrderBook book;
    book.submitLimitOrder(makeOrder(Side::SELL, 15100, 100));
    uint64_t id = book.submitMarketOrder(makeOrder(Side::BUY, 0, 50));
    assert(id == 2);                    // second id assigned (after the ask)
    assert(book.restingOrderCount() == 1);  // ask still resting
    assert(book.bestAsk() == 15100);
    assert(book.lastTrades().empty());  // no trades from stub matchOrder
}

int main() {
    test_empty_book();         std::printf("  ok  test_empty_book\n");
    test_add_single_limit();   std::printf("  ok  test_add_single_limit\n");
    test_two_sides();          std::printf("  ok  test_two_sides\n");
    test_cancel();             std::printf("  ok  test_cancel\n");
    test_fifo_within_level();  std::printf("  ok  test_fifo_within_level\n");
    test_market_order_stub();  std::printf("  ok  test_market_order_stub\n");

    std::printf("\nAll Block 2 tests passed.\n");
    return 0;
}