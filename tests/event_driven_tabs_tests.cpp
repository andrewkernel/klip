#include "klip/ui/event_driven_tabs.h"

#include <array>
#include <iostream>
#include <utility>

int ClickAfterIdle(bool fixed_tabs) {
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.DisplaySize = {800, 600};
  io.DeltaTime = 1.0F / 30.0F;
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  io.Fonts->SetTexID(1);
  int requested = -1, visible = -1;
  std::array<ImVec2, 3> centers{};
  const auto frame = [&] {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({700, 300});
    ImGui::Begin("test settings", nullptr, ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::BeginTabBar("settings")) {
      const int select = std::exchange(requested, -1);
      constexpr const char* labels[] = {"compatibility", "capture", "audio"};
      for (int i = 0; i < 3; ++i) {
        const bool content = fixed_tabs ? klip::BeginEventDrivenTab(labels[i], i, select, requested)
                                       : ImGui::BeginTabItem(labels[i]);
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        centers[i] = {(min.x + max.x) / 2, (min.y + max.y) / 2};
        if (content) { visible = i; ImGui::Text("content %d", i); ImGui::EndTabItem(); }
      }
      ImGui::EndTabBar();
    }
    ImGui::End();
    ImGui::Render();
  };
  io.AddMousePosEvent(750, 550);
  for (unsigned i = 0; i < klip::kInputRedrawFrames; ++i) frame();
  // One batched move/down/up, as a rapid click after an event-driven idle wait.
  io.AddMousePosEvent(centers[1].x, centers[1].y);
  io.AddMouseButtonEvent(0, true);
  io.AddMouseButtonEvent(0, false);
  for (unsigned i = 0; i < klip::kInputRedrawFrames; ++i) frame();
  const int result = visible;
  ImGui::DestroyContext();
  return result;
}

int main() {
  if (ClickAfterIdle(false) != 0) {
    std::cerr << "Fixture did not reproduce stock tabs missing the initial click\n"; return 1;
  }
  if (ClickAfterIdle(true) != 1) {
    std::cerr << "Event-driven tab selection did not show the clicked tab within the redraw burst\n"; return 1;
  }
  std::cout << "Idle move+click regression reproduced and fixed within the bounded redraw burst\n";
  return 0;
}
