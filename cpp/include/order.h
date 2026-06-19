#pragma once // say you had two other headers refering this header, the the below class/struct would be created twice causing the compiler to throw errors,
// this basically just ensures that this file is executed only once irrespective of how many calls are made!


#include <cstdint>

// Which side of the book an order belongs to.
enum class Side : uint8_t { // enum basically just assigns 0-> BUY, and 1-> SELL
    BUY,
    SELL
}; // It is simply a coding convience so that you do not have to remember what was 0 and what was 1
// Why we define a class is so that the assigned integers become scoped to the name
//  A person may very well want to reuse these elsewhere say in some other structure, it makes it
// easier for a person to understand whats going on , i.e
// Side::BUY, Something_Else::BUY , makes it clearer for verification, debugging and overall code clarity




// An order resting in (or about to enter) the book.

// PRICE here is stored redundantly, as of now we do not have a MODIFY option
// so once a price is given it is permanent
// IF we were to implement modify with this model, either remove price and always lookup in the map
// or implement it as a CANCEL prev + ADD new as modified

struct Order {
    uint64_t id;         // Stored to implement the CANCEL feature
    Side side;           // BUY/SELL 
    int64_t price;       // cents (not dollars , 150$ -> 15000) (why? cuz float never stores exact)
    int64_t quantity;    // remaining, not original
    uint64_t timestamp;  // elapsed nanoseconds since start
                          
};

// An order is an active event, ongoing life cycle

// Trade is just the record, a completed event

struct Trade {
    uint64_t timestamp; // when the trade happened (from start)
    uint64_t aggressiveId; // The ID which caused the match, i.e. crossed the market spread
    uint64_t passiveId; // Initial lister
    int64_t price; // Always the passiver persons price, (why? cuz the agressor may have given a worse price i.e. say 15000 was the ask, the agressor bid at 15020, the trade happened at 15000 not the latter)
    int64_t quantity; // this is obvio <3
};