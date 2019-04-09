#include "catch2/catch.hpp"

#include "../misc/ThreadSafeRingBuffer.hpp"

TEST_CASE("Thread safe ring buffer test", "[ringbuffer]")
{
    using ring_buffer_type = hwm::SingleChannelThreadSafeRingBuffer<int>;
    using ec_type = hwm::ThreadSafeRingBufferErrorCode;
    ring_buffer_type buffer(5);
    
    REQUIRE(buffer.GetCapacity() == 5);
    REQUIRE(buffer.GetNumPoppable() == 0);
    REQUIRE(buffer.GetNumPushable() == 5);
    
    int x = 10;
    buffer.Push(&x, 1);

    REQUIRE(buffer.GetCapacity() == 5);
    REQUIRE(buffer.GetNumPoppable() == 1);
    REQUIRE(buffer.GetNumPushable() == 4);
    
    int y = 0;
    buffer.PopOverwrite(&y, 1);
    
    REQUIRE(buffer.GetCapacity() == 5);
    REQUIRE(buffer.GetNumPoppable() == 0);
    REQUIRE(buffer.GetNumPushable() == 5);
    REQUIRE(y == 10);

    int xs[] = { 20, 21, 22, 23, 24 };
    REQUIRE((bool)buffer.Push(xs, 5) == true);
    
    REQUIRE(buffer.GetCapacity() == 5);
    REQUIRE(buffer.GetNumPoppable() == 5);
    REQUIRE(buffer.GetNumPushable() == 0);
    REQUIRE(buffer.Push(&x, 1).error_code() == ec_type::kBufferInsufficient);
    
    int ys[3] = { 100, 100, 100 };
    REQUIRE(buffer.PopAdd(ys, 3).error_code() == ec_type::kSuccessful);
    REQUIRE(ys[0] == xs[0] + 100);
    REQUIRE(ys[1] == xs[1] + 100);
    REQUIRE(ys[2] == xs[2] + 100);
    
    REQUIRE(buffer.GetCapacity() == 5);
    REQUIRE(buffer.GetNumPoppable() == 2);
    REQUIRE(buffer.GetNumPushable() == 3);
}
