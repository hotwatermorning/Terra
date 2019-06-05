#include "catch2/catch.hpp"

#include "../misc/Range.hpp"

TEST_CASE("Range algorithm test", "[range]")
{
    std::vector<int> xs { 10, 20, 30, 40, 50 };
    
    SECTION("find algorithms") {
        REQUIRE(hwm::find(xs, 20) == xs.begin() + 1);
        REQUIRE(hwm::find_if(xs, [](auto n) { return n == 40; }) == xs.begin() + 3);
    }
    
    SECTION("erase algorithms") {
        std::vector<int> const tmp = { 10, 30, 50 };
        hwm::erase_element(xs, 20);
        hwm::erase_element_if(xs, [](auto n) { return n == 40; });
        hwm::erase_element_if(xs, [](auto n) { return n == 70; });
        REQUIRE(tmp == xs);
    }
    
    SECTION("contain algorithm") {
        REQUIRE(hwm::contains(xs, 10) == true);
        REQUIRE(hwm::contains(xs, 11) == false);
        REQUIRE(hwm::contains_if(xs, [](auto x) { return x == 10; }) == true);
        REQUIRE(hwm::contains_if(xs, [](auto x) { return x == 11; }) == false);
    }
    
    SECTION("reversed") {
        auto xs = { 50, 40, 30, 20, 10 };
        std::vector<int> tmp;
        for(auto x: hwm::reversed(xs)) { tmp.push_back(x); }
        REQUIRE(tmp == std::vector<int>{10, 20, 30, 40, 50});
    }
}
