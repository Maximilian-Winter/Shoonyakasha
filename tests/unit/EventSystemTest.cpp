//
// EventSystemTest.cpp - Tests for EventDispatcher
//
// Tier 1: Pure unit tests — no GPU, no EnTT
//

#include <gtest/gtest.h>
#include "Core/EventSystem.h"

using namespace Shoonyakasha;

TEST(EventDispatcher, SubscribeAndPublish_Delivers) {
    EventDispatcher dispatcher;
    int received = 0;

    dispatcher.subscribe<KeyEvent>([&](const KeyEvent& e) {
        received = e.keyCode;
    });

    dispatcher.publish(KeyEvent(42, true));
    EXPECT_EQ(received, 42);
}

TEST(EventDispatcher, PublishWithNoSubscribers_NoOp) {
    EventDispatcher dispatcher;
    // Should not crash
    dispatcher.publish(KeyEvent(1, true));
    dispatcher.publish(WindowResizeEvent(800, 600));
}

TEST(EventDispatcher, MultipleSubscribers_AllReceive) {
    EventDispatcher dispatcher;
    int count = 0;

    dispatcher.subscribe<KeyEvent>([&](const KeyEvent&) { count++; });
    dispatcher.subscribe<KeyEvent>([&](const KeyEvent&) { count++; });
    dispatcher.subscribe<KeyEvent>([&](const KeyEvent&) { count++; });

    dispatcher.publish(KeyEvent(1, true));
    EXPECT_EQ(count, 3);
}

TEST(EventDispatcher, DifferentEventTypes_Isolated) {
    EventDispatcher dispatcher;
    bool keyReceived = false;
    bool mouseReceived = false;

    dispatcher.subscribe<KeyEvent>([&](const KeyEvent&) { keyReceived = true; });
    dispatcher.subscribe<MouseMoveEvent>([&](const MouseMoveEvent&) { mouseReceived = true; });

    dispatcher.publish(KeyEvent(1, true));
    EXPECT_TRUE(keyReceived);
    EXPECT_FALSE(mouseReceived);
}

TEST(EventDispatcher, WindowResizeEvent_DataCorrect) {
    EventDispatcher dispatcher;
    int w = 0, h = 0;

    dispatcher.subscribe<WindowResizeEvent>([&](const WindowResizeEvent& e) {
        w = e.width;
        h = e.height;
    });

    dispatcher.publish(WindowResizeEvent(1920, 1080));
    EXPECT_EQ(w, 1920);
    EXPECT_EQ(h, 1080);
}

TEST(EventDispatcher, KeyEvent_DataCorrect) {
    EventDispatcher dispatcher;
    int code = 0;
    bool pressed = false;

    dispatcher.subscribe<KeyEvent>([&](const KeyEvent& e) {
        code = e.keyCode;
        pressed = e.pressed;
    });

    dispatcher.publish(KeyEvent(65, true));
    EXPECT_EQ(code, 65);
    EXPECT_TRUE(pressed);

    dispatcher.publish(KeyEvent(66, false));
    EXPECT_EQ(code, 66);
    EXPECT_FALSE(pressed);
}

TEST(EventDispatcher, MouseMoveEvent_DataCorrect) {
    EventDispatcher dispatcher;
    int x = 0, y = 0;

    dispatcher.subscribe<MouseMoveEvent>([&](const MouseMoveEvent& e) {
        x = e.x;
        y = e.y;
    });

    dispatcher.publish(MouseMoveEvent(100, 200));
    EXPECT_EQ(x, 100);
    EXPECT_EQ(y, 200);
}

TEST(EventDispatcher, MouseButtonEvent_DataCorrect) {
    EventDispatcher dispatcher;
    int button = -1;
    bool pressed = false;

    dispatcher.subscribe<MouseButtonEvent>([&](const MouseButtonEvent& e) {
        button = e.button;
        pressed = e.pressed;
    });

    dispatcher.publish(MouseButtonEvent(0, true));
    EXPECT_EQ(button, 0);
    EXPECT_TRUE(pressed);
}

TEST(EventDispatcher, MouseScrollEvent_DataCorrect) {
    EventDispatcher dispatcher;
    double xOff = 0, yOff = 0;

    dispatcher.subscribe<MouseScrollEvent>([&](const MouseScrollEvent& e) {
        xOff = e.xOffset;
        yOff = e.yOffset;
    });

    dispatcher.publish(MouseScrollEvent(0.0, 1.5));
    EXPECT_DOUBLE_EQ(xOff, 0.0);
    EXPECT_DOUBLE_EQ(yOff, 1.5);
}

TEST(EventDispatcher, MultiplePublishes_AllDelivered) {
    EventDispatcher dispatcher;
    int count = 0;

    dispatcher.subscribe<KeyEvent>([&](const KeyEvent&) { count++; });

    dispatcher.publish(KeyEvent(1, true));
    dispatcher.publish(KeyEvent(2, false));
    dispatcher.publish(KeyEvent(3, true));
    EXPECT_EQ(count, 3);
}
