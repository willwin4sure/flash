#include <catch2/catch_test_macros.hpp>

#include "message.hpp"

#include <array>

TEST_CASE( "Message handles integers correctly in stack order with right sizes", "[message]" ) {
    enum class MessageId : uint32_t {
        KId0,
        KId1
    };

    flash::Message<MessageId> msg { MessageId::KId0 };

    msg << 1;
    msg << 2;

    REQUIRE( msg.size() == 8 + 2 * sizeof(int) );

    int a, b;
    msg >> b >> a;

    REQUIRE( a == 1 );
    REQUIRE( b == 2 );
}

TEST_CASE( "Messages handles floats, strings, structs, and arrays" , "[message]" ) {
    enum class MessageId : uint32_t {
        KId0,
        KId1
    };

    flash::Message<MessageId> msg { MessageId::KId0 };

    struct TestStruct {
        int a;
        int b;
    };

    msg << 1.0f;
    msg << "Hello, World!";
    msg << std::pair<int, int> { 1, 2 };
    msg << std::array<int, 3> {{ 1, 2, 3 }};
    msg << TestStruct { 1, 2 };
    msg << std::array<TestStruct, 2> {{ { 1, 2 }, { 3, 4 } }};
    
    REQUIRE( msg.size() == 8 + sizeof(float) + 14 + 2 * sizeof(int) + 3 * sizeof(int) + 3 * sizeof(TestStruct) );

    float f;
    std::array<char, 14> str;
    std::pair<int, int> pair;
    std::array<int, 3> arr3;
    TestStruct ts;
    std::array<TestStruct, 2> arr2;

    msg >> arr2 >> ts >> arr3 >> pair >> str >> f;

    REQUIRE( f == 1.0f );
    REQUIRE( strcmp(str.data(), "Hello, World!") == 0 );
    REQUIRE( (pair.first == 1 && pair.second == 2) );
    REQUIRE( (arr3[0] == 1 && arr3[1] == 2 && arr3[2] == 3) );
    REQUIRE( (ts.a == 1 && ts.b == 2) );
    REQUIRE( (arr2[0].a == 1 && arr2[0].b == 2) );
    REQUIRE( (arr2[1].a == 3 && arr2[1].b == 4) );
}