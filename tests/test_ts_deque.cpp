#include <catch2/catch_test_macros.hpp>

#include "flash/message.hpp"
#include "flash/ts_deque.hpp"

#include <iostream>

TEST_CASE( "Deque supports correct pushing and popping at back", "[ts_deque]" ) {
    flash::ts_deque<int> deque {};

    REQUIRE( deque.empty() == true );

    deque.push_back(1);
    deque.push_back(2);

    REQUIRE( deque.front() == 1 );
    REQUIRE( deque.back() == 2 );

    REQUIRE( deque.pop_back() == 2);

    REQUIRE( deque.front() == 1 );
    REQUIRE( deque.back() == 1 );

    REQUIRE( deque.pop_back() == 1 );

    deque.push_back(3);
    deque.push_back(3);

    REQUIRE( deque.front() == 3 );
    REQUIRE( deque.back() == 3 );

    REQUIRE( deque.size() == 2 );

    deque.clear();

    REQUIRE( deque.size() == 0 );
}

TEST_CASE( "Deque supports correct pushing and popping at front", "[ts_deque]" ) {
    flash::ts_deque<int> deque {};

    REQUIRE( deque.empty() == true );

    deque.push_front(1);

    REQUIRE( deque.front() == 1 );
    REQUIRE( deque.back() == 1 );

    REQUIRE( deque.pop_front() == 1);

    REQUIRE( deque.size() == 0 );
}

TEST_CASE( "Deque supports messages", "[ts_deque]" ) {
    enum class MessageId : uint32_t {
        KId0,
        KId1
    };

    flash::ts_deque<flash::message<MessageId>> deque {};

    flash::message<MessageId> msg { MessageId::KId0 };
    msg << 1.0 << 2.0;

    deque.push_back(std::move(msg));

    REQUIRE( deque.size() == 1 );
    REQUIRE( deque.front().size() == 8 + 2 * sizeof(double) );

    flash::message<MessageId> msg2 { MessageId::KId1 };
    msg2 << 3.0 << 4.0 << 5.0;

    deque.push_back(std::move(msg2));

    REQUIRE( deque.size() == 2 );
    REQUIRE( deque.back().size() == 8 + 3 * sizeof(double) );

    flash::message<MessageId> msg3 = deque.pop_front();

    REQUIRE( msg3.size() == 8 + 2 * sizeof(double) );

    double a, b;
    msg3 >> b >> a;

    REQUIRE( a == 1.0 );
    REQUIRE( b == 2.0 );

    deque.clear();

    REQUIRE( deque.size() == 0 );
}