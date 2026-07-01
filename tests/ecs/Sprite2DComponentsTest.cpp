//
// Sprite2DComponentsTest.cpp - Tests for UIAnchorComponent::resolve()
//
// Tier 2: ECS integration - uses glm, no GPU
//

#include <gtest/gtest.h>
#include "ECS/Sprite2DComponents.h"

using namespace Shoonyakasha;

TEST(UIAnchorComponent, TopLeft_NoOffset_IsScreenOrigin) {
    UIAnchorComponent anchor;
    anchor.anchor = UIAnchorComponent::Anchor::TopLeft;
    anchor.offsetPixels = glm::vec2(0.0f);

    glm::vec2 resolved = anchor.resolve(1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(resolved.x, 0.0f);
    EXPECT_FLOAT_EQ(resolved.y, 0.0f);
}

TEST(UIAnchorComponent, TopLeft_WithOffset_MovesIntoScreen) {
    UIAnchorComponent anchor;
    anchor.anchor = UIAnchorComponent::Anchor::TopLeft;
    anchor.offsetPixels = glm::vec2(100.0f, 30.0f);

    glm::vec2 resolved = anchor.resolve(1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(resolved.x, 100.0f);
    EXPECT_FLOAT_EQ(resolved.y, 30.0f);
}

TEST(UIAnchorComponent, BottomRight_NoOffset_IsScreenCorner) {
    UIAnchorComponent anchor;
    anchor.anchor = UIAnchorComponent::Anchor::BottomRight;
    anchor.offsetPixels = glm::vec2(0.0f);

    glm::vec2 resolved = anchor.resolve(1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(resolved.x, 1280.0f);
    EXPECT_FLOAT_EQ(resolved.y, 720.0f);
}

TEST(UIAnchorComponent, BottomRight_NegativeOffset_MovesIntoScreen) {
    UIAnchorComponent anchor;
    anchor.anchor = UIAnchorComponent::Anchor::BottomRight;
    anchor.offsetPixels = glm::vec2(-80.0f, -50.0f);

    glm::vec2 resolved = anchor.resolve(1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(resolved.x, 1200.0f);
    EXPECT_FLOAT_EQ(resolved.y, 670.0f);
}

TEST(UIAnchorComponent, MiddleCenter_IsScreenCenter) {
    UIAnchorComponent anchor;
    anchor.anchor = UIAnchorComponent::Anchor::MiddleCenter;
    anchor.offsetPixels = glm::vec2(0.0f);

    glm::vec2 resolved = anchor.resolve(1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(resolved.x, 640.0f);
    EXPECT_FLOAT_EQ(resolved.y, 360.0f);
}

TEST(Sprite2DComponent, DefaultsToWorldSpace) {
    Sprite2DComponent sprite;
    EXPECT_FALSE(sprite.screenSpace);
}

TEST(Text2DComponent, DefaultValues) {
    Text2DComponent text;
    EXPECT_TRUE(text.text.empty());
    EXPECT_FLOAT_EQ(text.fontSize, 24.0f);
    EXPECT_EQ(text.hAlign, Text2DComponent::HAlign::Left);
    EXPECT_TRUE(text.visible);
}
