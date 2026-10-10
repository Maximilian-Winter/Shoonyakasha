//
// FrameRingBufferTest.cpp - growth and alignment of the per-frame ring buffer
//

#include <gtest/gtest.h>

#include "GPU/FrameRingBuffer.h"

using Shoonyakasha::FrameRingBuffer;

TEST(FrameRingBuffer, KeepsACapacityThatFits) {
    EXPECT_EQ(FrameRingBuffer::grownCapacity(0, 0), 0u);
    EXPECT_EQ(FrameRingBuffer::grownCapacity(128 * 1024, 100 * 1024), 128u * 1024);
    EXPECT_EQ(FrameRingBuffer::grownCapacity(128 * 1024, 128 * 1024), 128u * 1024);
}

TEST(FrameRingBuffer, GrowsToAPowerOfTwoOfAtLeastTheMinimum) {
    EXPECT_EQ(FrameRingBuffer::grownCapacity(0, 1), FrameRingBuffer::kMinCapacity);
    EXPECT_EQ(FrameRingBuffer::grownCapacity(0, FrameRingBuffer::kMinCapacity), FrameRingBuffer::kMinCapacity);
    EXPECT_EQ(FrameRingBuffer::grownCapacity(0, FrameRingBuffer::kMinCapacity + 1), 2 * FrameRingBuffer::kMinCapacity);
    EXPECT_EQ(FrameRingBuffer::grownCapacity(64 * 1024, 1000 * 1000), 1024u * 1024);
}

TEST(FrameRingBuffer, AlignsUp) {
    EXPECT_EQ(FrameRingBuffer::alignUp(0, 16), 0u);
    EXPECT_EQ(FrameRingBuffer::alignUp(1, 16), 16u);
    EXPECT_EQ(FrameRingBuffer::alignUp(16, 16), 16u);
    EXPECT_EQ(FrameRingBuffer::alignUp(17, 16), 32u);
    EXPECT_EQ(FrameRingBuffer::alignUp(7, 1), 7u);
    EXPECT_EQ(FrameRingBuffer::alignUp(7, 0), 7u);
}
