#include "catch2/catch.hpp"

#include "../misc/TransitionalVolume.hpp"

TEST_CASE("Transitional volume test", "[transitional]")
{
    using namespace hwm;
    
    TransitionalVolume tr;
    tr = TransitionalVolume(96000, 1000, -10, 10);
    
    REQUIRE(tr.get_max_db() == 10);
    REQUIRE(tr.get_min_db() == -10);
    REQUIRE(tr.get_target_db() == 0);
    REQUIRE(tr.get_current_db() == 0);
    REQUIRE(tr.get_current_linear_gain() == 1.0);
    
    tr.set_target_db(1000);
    REQUIRE(tr.get_target_db() == tr.get_max_db());
    
    tr.set_target_db(-1000);
    REQUIRE(tr.get_target_db() == tr.get_min_db());
    
    tr.set_target_db(-5);
    REQUIRE(tr.get_target_db() == -5);
    REQUIRE(tr.get_current_db() == 0);
    
    // 1サンプルでどれだけdB値が変化するか
    auto const transition_per_step = log10(2) * 20 / (1000 / 1000 * 96000);
    auto const kTolerance = 0.000001;
    
    auto const cur = tr.get_current_db();
    
    tr.update_transition(1);
    REQUIRE(fabs(tr.get_current_db() - (cur - transition_per_step * 1)) < kTolerance);
    tr.update_transition(10);
    REQUIRE(fabs(tr.get_current_db() - (cur - transition_per_step * 11)) < kTolerance);
    tr.update_transition(100);
    REQUIRE(fabs(tr.get_current_db() - (cur - transition_per_step * 111)) < kTolerance);
    
    tr.update_transition(96000 * 4);
    REQUIRE(std::fabs(tr.get_current_db() - tr.get_target_db()) < kTolerance);
    REQUIRE(tr.get_current_linear_gain() != 0);
    
    tr.set_target_db(2.5);
    tr.update_transition(96000 * 4);
    REQUIRE(std::fabs(tr.get_current_db() - tr.get_target_db()) < kTolerance);
    REQUIRE(tr.get_current_linear_gain() != 0);
    
    tr.set_target_db(-100);
    tr.update_transition(96000 * 4);
    REQUIRE(std::fabs(tr.get_current_db() - tr.get_min_db()) < kTolerance);
    REQUIRE(tr.get_current_linear_gain() == 0);
    
    tr.set_target_db(100);
    tr.update_transition(96000 * 4);
    REQUIRE(std::fabs(tr.get_current_db() - tr.get_max_db()) < kTolerance);
    REQUIRE(tr.get_current_linear_gain() != 0);
}
