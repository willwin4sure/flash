#include <catch2/catch_test_macros.hpp>

#include "add.hpp"

TEST_CASE( "Addition is computed correctly", "[add]" ) {
    REQUIRE( add(1, 1) == 2 );
    REQUIRE( add(2, 2) == 4 );
    REQUIRE( add(-1, -5) == -6 );
    REQUIRE( add(-3, 6) == 3 );
}