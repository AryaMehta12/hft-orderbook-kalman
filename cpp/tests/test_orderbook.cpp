#include "OrderBook.h"

#include <cassert>
#include <cstdio>

// ---------------------------------------------------------------------------
// Helper: construct an order with required fields set. id=0 (engine assigns);
// timestamp defaults to 0 since most tests don't depend on time semantics.
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

// ===========================================================================
// SECTION 1 — Structural tests (matchOrder not exercised meaningfully)
// ===========================================================================

static void test_empty_book() {
    OrderBook book;
    assert(book.bestBid() == 0);
    assert(book.bestAsk() == 0);
    assert(book.midPrice() == 0);
    assert(book.restingOrderCount() == 0);
    assert(book.lastTrades().empty());
}

static void test_add_single_limit() {
    OrderBook book;
    uint64_t id = book.submitLimitOrder(makeOrder(Side::BUY, 15000, 100));
    assert(id == 1);
    assert(book.bestBid() == 15000);
    assert(book.bestAsk() == 0);
    assert(book.restingOrderCount() == 1);
    assert(book.lastTrades().empty());  // no opposite side to cross
}

static void test_two_sides() {
    OrderBook book;
    book.submitLimitOrder(makeOrder(Side::BUY,  14900, 100));
    book.submitLimitOrder(makeOrder(Side::BUY,  15000, 200));
    book.submitLimitOrder(makeOrder(Side::SELL, 15200, 150));
    book.submitLimitOrder(makeOrder(Side::SELL, 15100, 50));

    assert(book.bestBid() == 15000);
    assert(book.bestAsk() == 15100);
    assert(book.midPrice() == 15050);
    assert(book.restingOrderCount() == 4);
}

static void test_cancel() {
    OrderBook book;
    uint64_t id1 = book.submitLimitOrder(makeOrder(Side::BUY, 15000, 100));
    uint64_t id2 = book.submitLimitOrder(makeOrder(Side::BUY, 14900, 50));
    assert(book.restingOrderCount() == 2);

    assert(book.cancelOrder(id1) == true);
    assert(book.restingOrderCount() == 1);
    assert(book.bestBid() == 14900);

    assert(book.cancelOrder(9999) == false);
    assert(book.cancelOrder(id1)  == false);

    assert(book.cancelOrder(id2) == true);
    assert(book.restingOrderCount() == 0);
    assert(book.bestBid() == 0);
}

static void test_fifo_within_level() {
    OrderBook book;
    book.submitLimitOrder(makeOrder(Side::BUY, 15000, 100, 1));
    book.submitLimitOrder(makeOrder(Side::BUY, 15000, 200, 2));
    book.submitLimitOrder(makeOrder(Side::BUY, 15000, 50,  3));

    assert(book.restingOrderCount() == 3);
    assert(book.bestBid() == 15000);
}

static void test_market_order_empty_book() {
    OrderBook book;
    // Market BUY against empty asks_ — nothing to match, fully discarded.
    uint64_t id = book.submitMarketOrder(makeOrder(Side::BUY, 0, 100));
    assert(id == 1);
    assert(book.restingOrderCount() == 0);
    assert(book.lastTrades().empty());
}

// ===========================================================================
// SECTION 2 — Matching tests (exercise matchOrder in earnest)
// ===========================================================================

// Limit BUY at the same price as best ask → crosses, full fill of one resting.
static void test_limit_buy_full_consumes_one_ask() {
    OrderBook book;
    uint64_t ask_id  = book.submitLimitOrder(makeOrder(Side::SELL, 15100, 100));
    uint64_t buy_id  = book.submitLimitOrder(makeOrder(Side::BUY,  15100, 100));

    // After the match: both orders consumed, book empty.
    assert(book.restingOrderCount() == 0);
    assert(book.bestBid() == 0);
    assert(book.bestAsk() == 0);

    // One trade produced by the second submit; trade lives in lastTrades().
    const auto& trades = book.lastTrades();
    assert(trades.size() == 1);
    assert(trades[0].aggressiveId == buy_id);
    assert(trades[0].passiveId    == ask_id);
    assert(trades[0].price        == 15100);
    assert(trades[0].quantity     == 100);
}

// Limit BUY larger than best ask → consumes ask fully, leaves a resting bid.
static void test_limit_buy_partial_then_rest() {
    OrderBook book;
    uint64_t ask_id = book.submitLimitOrder(makeOrder(Side::SELL, 15100, 60));
    uint64_t buy_id = book.submitLimitOrder(makeOrder(Side::BUY,  15100, 100));

    // 60 of the buy matched, 40 remain — rested at 15100 as new best bid.
    assert(book.restingOrderCount() == 1);
    assert(book.bestBid() == 15100);
    assert(book.bestAsk() == 0);

    const auto& trades = book.lastTrades();
    assert(trades.size() == 1);
    assert(trades[0].aggressiveId == buy_id);
    assert(trades[0].passiveId    == ask_id);
    assert(trades[0].quantity     == 60);
    assert(trades[0].price        == 15100);
}

// Limit BUY smaller than best ask → partial fill of resting ask, ask stays.
static void test_limit_buy_partial_fill_of_resting() {
    OrderBook book;
    uint64_t ask_id = book.submitLimitOrder(makeOrder(Side::SELL, 15100, 100));
    uint64_t buy_id = book.submitLimitOrder(makeOrder(Side::BUY,  15100, 40));

    // 40 traded, ask reduced to 60. Buy fully consumed (none rests).
    assert(book.restingOrderCount() == 1);
    assert(book.bestAsk() == 15100);
    assert(book.bestBid() == 0);

    const auto& trades = book.lastTrades();
    assert(trades.size() == 1);
    assert(trades[0].aggressiveId == buy_id);
    assert(trades[0].passiveId    == ask_id);
    assert(trades[0].quantity     == 40);
    assert(trades[0].price        == 15100);

    // Cancel what remains of the ask (60 left). Should succeed.
    assert(book.cancelOrder(ask_id) == true);
    assert(book.restingOrderCount() == 0);
}

// Non-crossing limit BUY → no trade, just rests.
static void test_limit_buy_no_cross() {
    OrderBook book;
    book.submitLimitOrder(makeOrder(Side::SELL, 15200, 100));
    uint64_t buy_id = book.submitLimitOrder(makeOrder(Side::BUY, 15100, 100));

    // Buy price 15100 < ask price 15200 → no cross. Both rest.
    assert(book.restingOrderCount() == 2);
    assert(book.bestBid() == 15100);
    assert(book.bestAsk() == 15200);
    assert(book.lastTrades().empty());
    (void)buy_id;
}

// Limit BUY walking multiple ask price levels.
static void test_limit_buy_walks_levels() {
    OrderBook book;
    book.submitLimitOrder(makeOrder(Side::SELL, 15100, 30));   // best ask
    book.submitLimitOrder(makeOrder(Side::SELL, 15200, 50));
    book.submitLimitOrder(makeOrder(Side::SELL, 15300, 100));

    // Buy 200 @ 15500: should walk all three levels, total fill = 180,
    // leftover 20 rests at 15500.
    uint64_t buy_id = book.submitLimitOrder(makeOrder(Side::BUY, 15500, 200));

    const auto& trades = book.lastTrades();
    assert(trades.size() == 3);
    assert(trades[0].price == 15100 && trades[0].quantity == 30);
    assert(trades[1].price == 15200 && trades[1].quantity == 50);
    assert(trades[2].price == 15300 && trades[2].quantity == 100);
    for (const auto& t : trades) {
        assert(t.aggressiveId == buy_id);
    }

    // Book should now hold only the leftover 20 at 15500 on the bid side.
    assert(book.bestBid() == 15500);
    assert(book.bestAsk() == 0);
    assert(book.restingOrderCount() == 1);
}

// Limit BUY stops at a non-crossing level mid-walk.
static void test_limit_buy_stops_at_noncrossing_level() {
    OrderBook book;
    book.submitLimitOrder(makeOrder(Side::SELL, 15100, 30));
    book.submitLimitOrder(makeOrder(Side::SELL, 15200, 50));
    book.submitLimitOrder(makeOrder(Side::SELL, 15300, 100));   // beyond limit

    // Buy 1000 @ 15250: fills 30 at 15100, 50 at 15200, then stops because
    // 15300 > 15250. Leftover 920 rests at 15250 on the bid side.
    uint64_t buy_id = book.submitLimitOrder(makeOrder(Side::BUY, 15250, 1000));

    const auto& trades = book.lastTrades();
    assert(trades.size() == 2);
    assert(trades[0].price == 15100 && trades[0].quantity == 30);
    assert(trades[1].price == 15200 && trades[1].quantity == 50);

    assert(book.bestBid() == 15250);
    assert(book.bestAsk() == 15300);  // untouched
    assert(book.restingOrderCount() == 2);
    (void)buy_id;
}

// FIFO priority within a price level: oldest matches first.
static void test_fifo_priority_during_match() {
    OrderBook book;
    uint64_t ask1 = book.submitLimitOrder(makeOrder(Side::SELL, 15100, 30, 1));  // oldest
    uint64_t ask2 = book.submitLimitOrder(makeOrder(Side::SELL, 15100, 40, 2));
    uint64_t ask3 = book.submitLimitOrder(makeOrder(Side::SELL, 15100, 50, 3));

    // Market buy for 50: should consume ask1 fully (30), then 20 of ask2.
    uint64_t buy_id = book.submitMarketOrder(makeOrder(Side::BUY, 0, 50));

    const auto& trades = book.lastTrades();
    assert(trades.size() == 2);
    assert(trades[0].passiveId == ask1);
    assert(trades[0].quantity  == 30);
    assert(trades[1].passiveId == ask2);
    assert(trades[1].quantity  == 20);
    (void)ask3;
    (void)buy_id;

    // ask2 should have 20 remaining; ask3 untouched.
    assert(book.restingOrderCount() == 2);
    assert(book.bestAsk() == 15100);
}

// Market BUY consumes a single level partially.
static void test_market_buy_partial() {
    OrderBook book;
    book.submitLimitOrder(makeOrder(Side::SELL, 15100, 100));
    uint64_t buy_id = book.submitMarketOrder(makeOrder(Side::BUY, 0, 40));

    const auto& trades = book.lastTrades();
    assert(trades.size() == 1);
    assert(trades[0].quantity == 40);
    assert(trades[0].price    == 15100);
    assert(book.restingOrderCount() == 1);
    assert(book.bestAsk() == 15100);
    (void)buy_id;
}

// Market BUY larger than entire ask side → fills what's available, rest discarded.
static void test_market_buy_exhausts_book() {
    OrderBook book;
    book.submitLimitOrder(makeOrder(Side::SELL, 15100, 50));
    book.submitLimitOrder(makeOrder(Side::SELL, 15200, 50));

    uint64_t buy_id = book.submitMarketOrder(makeOrder(Side::BUY, 0, 1000));

    const auto& trades = book.lastTrades();
    assert(trades.size() == 2);
    assert(trades[0].quantity == 50 && trades[0].price == 15100);
    assert(trades[1].quantity == 50 && trades[1].price == 15200);

    // Ask side completely exhausted; remainder (900) discarded (IOC).
    assert(book.restingOrderCount() == 0);
    assert(book.bestAsk() == 0);
    (void)buy_id;
}

// Symmetric sanity: SELL side matches against BIDs the same way.
static void test_limit_sell_walks_bids() {
    OrderBook book;
    book.submitLimitOrder(makeOrder(Side::BUY, 15000, 30));
    book.submitLimitOrder(makeOrder(Side::BUY, 14900, 40));
    book.submitLimitOrder(makeOrder(Side::BUY, 14800, 50));

    // Sell 100 @ 14850: should fill 30 at 15000, 40 at 14900, then stop
    // (14800 < 14850). Leftover 30 rests at 14850 on the ask side.
    uint64_t sell_id = book.submitLimitOrder(makeOrder(Side::SELL, 14850, 100));

    const auto& trades = book.lastTrades();
    assert(trades.size() == 2);
    assert(trades[0].price == 15000 && trades[0].quantity == 30);
    assert(trades[1].price == 14900 && trades[1].quantity == 40);

    assert(book.bestAsk() == 14850);
    assert(book.bestBid() == 14800);
    assert(book.restingOrderCount() == 2);
    (void)sell_id;
}

// ===========================================================================
// main
// ===========================================================================

int main() {
    // Structural
    test_empty_book();                    std::printf("  ok  test_empty_book\n");
    test_add_single_limit();              std::printf("  ok  test_add_single_limit\n");
    test_two_sides();                     std::printf("  ok  test_two_sides\n");
    test_cancel();                        std::printf("  ok  test_cancel\n");
    test_fifo_within_level();             std::printf("  ok  test_fifo_within_level\n");
    test_market_order_empty_book();       std::printf("  ok  test_market_order_empty_book\n");

    // Matching
    test_limit_buy_full_consumes_one_ask();    std::printf("  ok  test_limit_buy_full_consumes_one_ask\n");
    test_limit_buy_partial_then_rest();        std::printf("  ok  test_limit_buy_partial_then_rest\n");
    test_limit_buy_partial_fill_of_resting();  std::printf("  ok  test_limit_buy_partial_fill_of_resting\n");
    test_limit_buy_no_cross();                 std::printf("  ok  test_limit_buy_no_cross\n");
    test_limit_buy_walks_levels();             std::printf("  ok  test_limit_buy_walks_levels\n");
    test_limit_buy_stops_at_noncrossing_level();std::printf("  ok  test_limit_buy_stops_at_noncrossing_level\n");
    test_fifo_priority_during_match();         std::printf("  ok  test_fifo_priority_during_match\n");
    test_market_buy_partial();                 std::printf("  ok  test_market_buy_partial\n");
    test_market_buy_exhausts_book();           std::printf("  ok  test_market_buy_exhausts_book\n");
    test_limit_sell_walks_bids();              std::printf("  ok  test_limit_sell_walks_bids\n");

    std::printf("\nAll tests passed (15/15).\n");
    return 0;
}