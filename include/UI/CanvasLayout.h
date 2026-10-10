//
// CanvasLayout.h - Resolving UIRect placement for a canvas tree
//

#pragma once

#include "UI/UIComponents.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Shoonyakasha {
namespace UI {

/// The overlap of two rects; empty (max == min) when they do not overlap.
Rect intersect(const Rect& a, const Rect& b);

/// Where `element` lies inside `parent`.
Rect resolveRect(const Rect& parent, const UIRect& element);

/// Target pixels per canvas unit for a canvas drawn into `targetSize` pixels.
/// Returns 1 for a target with a zero or negative side.
float canvasScale(const UICanvas& canvas, const glm::vec2& targetSize);

/// Lays out one canvas: writes its targetSize, size and scale, and the
/// computed fields of every UIRect below it. Screen-overlay canvases are
/// sized from `screenSize`, world canvases from their pixelSize. Descendants
/// without UIRect, and nested canvases, are not entered.
void layoutCanvas(entt::registry& registry, entt::entity canvas, const glm::vec2& screenSize);

} // namespace UI
} // namespace Shoonyakasha
