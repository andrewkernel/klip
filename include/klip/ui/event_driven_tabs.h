#pragma once

#include <imgui.h>

namespace klip {
inline constexpr unsigned kInputRedrawFrames = 4;

inline bool BeginEventDrivenTab(const char* label, int index, int requested, int& next_request) {
  const bool visible = ImGui::BeginTabItem(label, nullptr,
      requested == index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None);
  // Tabs without close buttons need not wait for the previous hovered frame.
  // Stock tab overlap handling can otherwise miss a quick move+click after idle.
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem) && ImGui::IsMouseClicked(0))
    next_request = index;
  return visible;
}
}  // namespace klip
