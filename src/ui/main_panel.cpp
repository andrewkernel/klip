#include "klip/ui/main_panel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace klip {
namespace {

constexpr ImVec4 kMuted{0.58F, 0.61F, 0.69F, 1.0F};
constexpr ImVec4 kLavender{0.68F, 0.33F, 1.0F, 1.0F};
constexpr ImVec4 kMint{0.22F, 0.84F, 0.49F, 1.0F};
constexpr ImVec4 kCoral{1.0F, 0.31F, 0.38F, 1.0F};
constexpr ImU32 kBackgroundTop = IM_COL32(12, 17, 25, 255);
constexpr ImU32 kBackgroundBottom = IM_COL32(8, 12, 19, 255);
constexpr ImU32 kPanel = IM_COL32(16, 22, 31, 245);
constexpr ImU32 kPanelHover = IM_COL32(21, 28, 39, 255);
constexpr ImU32 kPanelBorder = IM_COL32(44, 51, 64, 255);
constexpr ImU32 kDivider = IM_COL32(39, 46, 57, 210);
constexpr ImU32 kTextU32 = IM_COL32(238, 240, 247, 255);
constexpr ImU32 kMutedU32 = IM_COL32(151, 157, 173, 255);
constexpr ImU32 kPurple = IM_COL32(150, 72, 255, 255);
constexpr ImU32 kPurpleBright = IM_COL32(184, 91, 255, 255);
constexpr ImU32 kGreen = IM_COL32(57, 211, 124, 255);
constexpr ImU32 kRed = IM_COL32(255, 78, 93, 255);

const char* StatusText(CaptureStatus status) {
  switch (status) {
    case CaptureStatus::kIdle: return "IDLE";
    case CaptureStatus::kStarting: return "STARTING";
    case CaptureStatus::kBuffering: return "BUFFER LIVE";
    case CaptureStatus::kSaving: return "SAVING CLIP";
    case CaptureStatus::kStopping: return "STOPPING";
    case CaptureStatus::kFailed: return "NEEDS ATTENTION";
  }
  return "IDLE";
}

ImVec4 StatusColor(CaptureStatus status) {
  if (status == CaptureStatus::kBuffering) return kMint;
  if (status == CaptureStatus::kSaving) return {1.0F, 0.73F, 0.25F, 1.0F};
  if (status == CaptureStatus::kFailed) return kCoral;
  return kMuted;
}

void SectionLabel(const char* text) {
  ImGui::Spacing();
  ImGui::TextColored(kMuted, "%s", text);
  ImGui::Separator();
}

ImFont* UiFont(std::size_t index) {
  const auto& fonts = ImGui::GetIO().Fonts->Fonts;
  return index < static_cast<std::size_t>(fonts.Size) ? fonts[static_cast<int>(index)]
                                                       : ImGui::GetFont();
}

void DrawText(ImDrawList* draw, ImFont* font, float size, ImVec2 position, ImU32 color,
              std::string_view text) {
  draw->AddText(font, size, position, color, text.data(), text.data() + text.size());
}

float TextWidth(ImFont* font, float size, std::string_view text) {
  return font->CalcTextSizeA(size, 10000.0F, 0.0F, text.data(), text.data() + text.size()).x;
}

void DrawCard(ImDrawList* draw, ImVec2 minimum, ImVec2 size, bool hovered = false,
              ImU32 fill = kPanel, ImU32 border = kPanelBorder, float rounding = 12.0F) {
  const ImVec2 maximum{minimum.x + size.x, minimum.y + size.y};
  draw->AddRectFilled(minimum, maximum, hovered ? kPanelHover : fill, rounding);
  draw->AddRect(minimum, maximum, border, rounding, 0, 1.0F);
}

void DrawLogo(ImDrawList* draw, ImVec2 position, float scale) {
  const ImU32 left = IM_COL32(139, 67, 247, 255);
  const ImU32 light = IM_COL32(177, 90, 255, 255);
  const ImU32 dark = IM_COL32(103, 48, 219, 255);
  draw->AddRectFilled(position, {position.x + 11.0F * scale, position.y + 34.0F * scale}, left,
                      4.0F * scale);
  const ImVec2 upper[] = {{position.x + 8.0F * scale, position.y + 15.0F * scale},
                          {position.x + 24.0F * scale, position.y},
                          {position.x + 35.0F * scale, position.y},
                          {position.x + 17.0F * scale, position.y + 18.0F * scale}};
  draw->AddConvexPolyFilled(upper, 4, light);
  const ImVec2 lower[] = {{position.x + 9.0F * scale, position.y + 18.0F * scale},
                          {position.x + 18.0F * scale, position.y + 10.0F * scale},
                          {position.x + 36.0F * scale, position.y + 34.0F * scale},
                          {position.x + 23.0F * scale, position.y + 34.0F * scale}};
  draw->AddConvexPolyFilled(lower, 4, dark);
}

void DrawMonitorIcon(ImDrawList* draw, ImVec2 center, ImU32 color) {
  draw->AddRect({center.x - 15.0F, center.y - 12.0F}, {center.x + 15.0F, center.y + 8.0F},
                color, 3.0F, 0, 2.0F);
  draw->AddLine({center.x, center.y + 8.0F}, {center.x, center.y + 14.0F}, color, 2.0F);
  draw->AddLine({center.x - 8.0F, center.y + 14.0F}, {center.x + 8.0F, center.y + 14.0F},
                color, 2.0F);
}

void DrawControllerIcon(ImDrawList* draw, ImVec2 center, ImU32 color) {
  draw->AddRect({center.x - 16.0F, center.y - 9.0F}, {center.x + 16.0F, center.y + 10.0F},
                color, 7.0F, 0, 2.0F);
  draw->AddLine({center.x - 10.0F, center.y}, {center.x - 4.0F, center.y}, color, 2.0F);
  draw->AddLine({center.x - 7.0F, center.y - 3.0F}, {center.x - 7.0F, center.y + 3.0F},
                color, 2.0F);
  draw->AddCircleFilled({center.x + 7.0F, center.y - 2.0F}, 1.8F, color);
  draw->AddCircleFilled({center.x + 11.0F, center.y + 2.0F}, 1.8F, color);
}

void DrawSpeakerIcon(ImDrawList* draw, ImVec2 center, ImU32 color) {
  const ImVec2 speaker[] = {{center.x - 15.0F, center.y - 5.0F},
                            {center.x - 9.0F, center.y - 5.0F},
                            {center.x - 2.0F, center.y - 12.0F},
                            {center.x - 2.0F, center.y + 12.0F},
                            {center.x - 9.0F, center.y + 5.0F},
                            {center.x - 15.0F, center.y + 5.0F}};
  draw->AddPolyline(speaker, 6, color, ImDrawFlags_Closed, 2.0F);
  draw->PathArcTo(center, 9.0F, -0.8F, 0.8F, 16);
  draw->PathStroke(color, 0, 2.0F);
  draw->PathArcTo(center, 15.0F, -0.7F, 0.7F, 16);
  draw->PathStroke(color, 0, 2.0F);
}

void DrawMicrophoneIcon(ImDrawList* draw, ImVec2 center, ImU32 color) {
  draw->AddRect({center.x - 6.0F, center.y - 13.0F}, {center.x + 6.0F, center.y + 5.0F},
                color, 6.0F, 0, 2.0F);
  draw->PathArcTo(center, 12.0F, 0.15F, 3.0F, 24);
  draw->PathStroke(color, 0, 2.0F);
  draw->AddLine({center.x, center.y + 12.0F}, {center.x, center.y + 17.0F}, color, 2.0F);
  draw->AddLine({center.x - 7.0F, center.y + 17.0F}, {center.x + 7.0F, center.y + 17.0F},
                color, 2.0F);
}

void DrawRecordIcon(ImDrawList* draw, ImVec2 center, ImU32 color) {
  draw->AddCircle(center, 15.0F, color, 32, 2.5F);
  draw->AddCircleFilled(center, 6.0F, color);
}

void DrawClipIcon(ImDrawList* draw, ImVec2 center, ImU32 color) {
  draw->AddRect({center.x - 13.0F, center.y - 12.0F}, {center.x + 13.0F, center.y + 12.0F},
                color, 4.0F, 0, 2.0F);
  draw->AddLine({center.x - 6.0F, center.y - 3.0F}, {center.x + 6.0F, center.y - 3.0F},
                color, 2.0F);
  draw->AddLine({center.x + 6.0F, center.y - 3.0F}, {center.x + 3.0F, center.y - 6.0F},
                color, 2.0F);
  draw->AddLine({center.x + 6.0F, center.y - 3.0F}, {center.x + 3.0F, center.y}, color,
                2.0F);
  draw->AddLine({center.x + 6.0F, center.y + 4.0F}, {center.x - 6.0F, center.y + 4.0F},
                color, 2.0F);
}

void DrawFolderIcon(ImDrawList* draw, ImVec2 center, ImU32 color) {
  draw->AddRect({center.x - 13.0F, center.y - 8.0F}, {center.x + 13.0F, center.y + 10.0F},
                color, 3.0F, 0, 2.0F);
  draw->AddLine({center.x - 11.0F, center.y - 9.0F}, {center.x - 3.0F, center.y - 9.0F},
                color, 2.0F);
  draw->AddLine({center.x - 3.0F, center.y - 9.0F}, {center.x + 1.0F, center.y - 5.0F},
                color, 2.0F);
}

void DrawChevron(ImDrawList* draw, ImVec2 center, ImU32 color) {
  draw->AddLine({center.x - 6.0F, center.y - 3.0F}, {center.x, center.y + 3.0F}, color,
                2.0F);
  draw->AddLine({center.x, center.y + 3.0F}, {center.x + 6.0F, center.y - 3.0F}, color,
                2.0F);
}

void DrawProgress(ImDrawList* draw, ImVec2 minimum, ImVec2 size, float level,
                  ImU32 active = kPurpleBright) {
  draw->AddRectFilled(minimum, {minimum.x + size.x, minimum.y + size.y},
                      IM_COL32(35, 42, 52, 255), size.y * 0.5F);
  const float width = size.x * std::clamp(level, 0.0F, 1.0F);
  if (width > 0.5F)
    draw->AddRectFilled(minimum, {minimum.x + width, minimum.y + size.y}, active,
                        size.y * 0.5F);
}

bool Switch(const char* id, ImVec2 position, bool enabled) {
  ImGui::SetCursorScreenPos(position);
  ImGui::InvisibleButton(id, {48.0F, 26.0F});
  const bool clicked = ImGui::IsItemClicked();
  auto* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(position, {position.x + 48.0F, position.y + 26.0F},
                      enabled ? kPurple : IM_COL32(75, 82, 95, 255), 13.0F);
  const float knob_x = position.x + (enabled ? 35.0F : 13.0F);
  draw->AddCircleFilled({knob_x, position.y + 13.0F}, 10.0F, IM_COL32(232, 235, 241, 255));
  return clicked;
}

bool GainSlider(const char* id, ImVec2 position, float width, float& gain) {
  constexpr float height = 22.0F;
  ImGui::SetCursorScreenPos(position);
  ImGui::InvisibleButton(id, {width, height});
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  bool changed = false;
  if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    const float normalized = std::clamp((ImGui::GetIO().MousePos.x - position.x) / width, 0.0F,
                                        1.0F);
    const float updated = normalized * 2.0F;
    if (std::abs(updated - gain) > 0.001F) {
      gain = updated;
      changed = true;
    }
  }
  auto* draw = ImGui::GetWindowDrawList();
  const float track_y = position.y + height * 0.5F - 3.0F;
  draw->AddRectFilled({position.x, track_y}, {position.x + width, track_y + 6.0F},
                      IM_COL32(35, 42, 52, 255), 3.0F);
  const float progress = std::clamp(gain * 0.5F, 0.0F, 1.0F);
  const float knob_x = position.x + width * progress;
  if (progress > 0.001F)
    draw->AddRectFilled({position.x, track_y}, {knob_x, track_y + 6.0F}, kPurpleBright, 3.0F);
  if (hovered || active)
    draw->AddCircleFilled({knob_x, position.y + height * 0.5F}, 9.0F,
                          IM_COL32(173, 88, 255, 45));
  draw->AddCircleFilled({knob_x, position.y + height * 0.5F}, active ? 6.0F : 5.0F,
                        IM_COL32(220, 185, 255, 255));
  return changed;
}

bool PopupOption(const char* id, std::string_view label, bool selected) {
  const ImVec2 position = ImGui::GetCursorScreenPos();
  const ImVec2 size{ImGui::GetContentRegionAvail().x, 42.0F};
  ImGui::InvisibleButton(id, size);
  const bool clicked = ImGui::IsItemClicked();
  const bool hovered = ImGui::IsItemHovered();
  auto* draw = ImGui::GetWindowDrawList();
  if (selected || hovered) {
    draw->AddRectFilled(position, {position.x + size.x, position.y + size.y},
                        selected ? IM_COL32(102, 48, 178, 150) : IM_COL32(255, 255, 255, 10),
                        8.0F);
  }
  draw->AddCircle({position.x + 20.0F, position.y + 21.0F}, 7.0F,
                  selected ? kPurpleBright : kMutedU32, 16, 1.5F);
  if (selected) draw->AddCircleFilled({position.x + 20.0F, position.y + 21.0F}, 3.5F,
                                     kPurpleBright);
  DrawText(draw, UiFont(1), 14.0F, {position.x + 39.0F, position.y + 12.0F},
           selected ? kTextU32 : kMutedU32, label);
  return clicked;
}

std::string Ellipsize(ImFont* font, float size, std::string_view text, float maximum_width) {
  if (TextWidth(font, size, text) <= maximum_width) return std::string(text);
  std::string shortened(text);
  constexpr std::string_view ellipsis = "...";
  while (!shortened.empty() &&
         TextWidth(font, size, shortened + std::string(ellipsis)) > maximum_width) {
    shortened.pop_back();
  }
  shortened += ellipsis;
  return shortened;
}

std::string CompactSourceLabel(std::string_view label) {
  const auto open = label.rfind('[');
  const auto close = label.rfind(']');
  if (open != std::string_view::npos && close != std::string_view::npos && close > open + 1)
    return std::string(label.substr(open + 1, close - open - 1));
  return std::string(label);
}

int EncoderProfile(const std::vector<std::string>& preferences) {
  if (preferences.size() > 1) return 0;
  if (preferences.empty()) return 0;
  if (preferences[0] == "h264_nvenc") return 1;
  if (preferences[0] == "h264_amf") return 2;
  if (preferences[0] == "h264_mf") return 3;
  return 0;
}

void SetEncoderProfile(AppConfig& config, int profile) {
  switch (profile) {
    case 1: config.encoder_preferences = {"h264_nvenc"}; break;
    case 2: config.encoder_preferences = {"h264_amf"}; break;
    case 3: config.encoder_preferences = {"h264_mf"}; break;
    default:
      config.encoder_preferences = {"h264_nvenc", "h264_amf", "h264_mf"};
      break;
  }
}

std::string PathText(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}

std::filesystem::path ParsePath(const char* text) {
  std::u8string utf8;
  for (const auto* byte = reinterpret_cast<const unsigned char*>(text); *byte != 0; ++byte)
    utf8.push_back(static_cast<char8_t>(*byte));
  return std::filesystem::path(utf8);
}

void ResizeReplayBudget(AppConfig& config) {
  config.rolling_buffer_seconds = config.clip_duration_seconds + 15.0;
  const auto bits_per_second =
      static_cast<double>(config.video_bitrate + config.audio_bitrate);
  const auto required = bits_per_second / 8.0 * config.rolling_buffer_seconds * 1.25;
  constexpr auto minimum = 32ULL * 1024ULL * 1024ULL;
  config.rolling_buffer_bytes =
      std::max<std::size_t>(minimum, static_cast<std::size_t>(required));
}

}  // namespace

void MainPanel::Render(const ApplicationSnapshot& snapshot, const AppConfig& config,
                       const UiCommands& commands, bool hotkeys_available) {
  constexpr ImVec2 size{1260.0F, 800.0F};
  auto* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  ImGui::SetNextWindowPos({viewport->WorkPos.x + (viewport->WorkSize.x - size.x) * 0.5F,
                           viewport->WorkPos.y + (viewport->WorkSize.y - size.y) * 0.5F},
                          ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      settings_open_ ? ImVec2{30.0F, 24.0F} : ImVec2{0.0F, 0.0F});
  ImGui::Begin("Klip", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

  if (settings_open_)
    RenderSettings(snapshot, config, commands);
  else
    RenderDashboard(snapshot, config, commands, hotkeys_available);
  ImGui::End();
  ImGui::PopStyleVar();
}

void MainPanel::RenderDashboard(const ApplicationSnapshot& snapshot, const AppConfig& config,
                                const UiCommands& commands, bool hotkeys_available) {
  auto* draw = ImGui::GetWindowDrawList();
  const ImVec2 window_min = ImGui::GetWindowPos();
  const ImVec2 window_size = ImGui::GetWindowSize();
  const ImVec2 window_max{window_min.x + window_size.x, window_min.y + window_size.y};
  draw->AddRectFilledMultiColor(window_min, window_max, kBackgroundTop, kBackgroundTop,
                                kBackgroundBottom, kBackgroundBottom);
  draw->AddCircleFilled({window_min.x + 180.0F, window_min.y + 70.0F}, 250.0F,
                        IM_COL32(75, 42, 128, 18), 64);
  draw->AddCircleFilled({window_max.x - 120.0F, window_min.y + 360.0F}, 310.0F,
                        IM_COL32(43, 71, 108, 12), 64);
  draw->AddRect(window_min, window_max, IM_COL32(48, 56, 70, 255), 14.0F, 0, 1.0F);

  auto* regular = UiFont(0);
  auto* semibold = UiFont(1);
  auto* title_font = UiFont(2);
  auto* number_font = UiFont(3);
  constexpr float margin_x = 42.0F;
  const float content_width = window_size.x - margin_x * 2.0F;
  const ImVec2 content{window_min.x + margin_x, window_min.y};

  DrawLogo(draw, {content.x, window_min.y + 28.0F}, 1.0F);
  DrawText(draw, title_font, 29.0F, {content.x + 50.0F, window_min.y + 29.0F}, kTextU32,
           "Klip");
  const char* status_text = snapshot.status == CaptureStatus::kBuffering
                                ? "READY"
                                : StatusText(snapshot.status);
  const ImU32 status_color = ImGui::ColorConvertFloat4ToU32(StatusColor(snapshot.status));
  const float status_width = TextWidth(semibold, 15.0F, status_text);
  const float status_x = content.x + content_width - status_width;
  draw->AddCircleFilled({status_x - 18.0F, window_min.y + 45.0F}, 5.5F, status_color);
  DrawText(draw, semibold, 15.0F, {status_x, window_min.y + 35.0F}, kMutedU32,
           status_text);

  const float hero_y = window_min.y + 96.0F;
  DrawText(draw, semibold, 14.0F, {content.x, hero_y}, kMutedU32, "CLIP LENGTH");
  char clip_seconds[24]{};
  std::snprintf(clip_seconds, sizeof(clip_seconds), "%.0f", config.clip_duration_seconds);
  DrawText(draw, number_font, 42.0F, {content.x, hero_y + 24.0F}, kTextU32, clip_seconds);
  const float seconds_x = content.x + TextWidth(number_font, 42.0F, clip_seconds) + 12.0F;
  DrawText(draw, regular, 17.0F, {seconds_x, hero_y + 48.0F}, kMutedU32, "seconds");
  char replay_copy[64]{};
  std::snprintf(replay_copy, sizeof(replay_copy), "Saves the last %.0f seconds",
                config.clip_duration_seconds);
  DrawText(draw, regular, 14.0F, {content.x, hero_y + 88.0F}, kMutedU32, replay_copy);
  const float hero_divider_x = content.x + 276.0F;
  draw->AddLine({hero_divider_x, hero_y}, {hero_divider_x, hero_y + 108.0F}, kDivider, 1.0F);

  const bool can_save = snapshot.status == CaptureStatus::kBuffering &&
                        !snapshot.finalizing_recording &&
                        snapshot.metrics.rolling_buffer_seconds >= 1.0;
  char save_label[80]{};
  std::snprintf(save_label, sizeof(save_label), "SAVE LAST %.0f SECONDS",
                config.clip_duration_seconds);
  const float action_gap = 26.0F;
  const float action_start_x = hero_divider_x + 58.0F;
  const float action_width = (content.x + content_width - action_start_x - action_gap) * 0.5F;
  const ImVec2 save_pos{action_start_x, hero_y + 1.0F};
  const ImVec2 action_size{action_width, 94.0F};
  ImGui::SetCursorScreenPos(save_pos);
  if (!can_save) ImGui::BeginDisabled();
  ImGui::InvisibleButton("##save-clip-card", action_size);
  const bool save_clicked = ImGui::IsItemClicked();
  const bool save_hovered = ImGui::IsItemHovered();
  if (!can_save) ImGui::EndDisabled();
  DrawCard(draw, save_pos, action_size, save_hovered && can_save,
           can_save ? IM_COL32(105, 43, 211, 255) : IM_COL32(48, 42, 62, 255),
           can_save ? kPurpleBright : kPanelBorder, 13.0F);
  draw->AddCircleFilled({save_pos.x + 42.0F, save_pos.y + 47.0F}, 22.0F,
                        IM_COL32(255, 255, 255, can_save ? 18 : 8));
  DrawClipIcon(draw, {save_pos.x + 42.0F, save_pos.y + 47.0F},
               can_save ? kTextU32 : kMutedU32);
  DrawText(draw, semibold, 17.0F, {save_pos.x + 75.0F, save_pos.y + 36.0F},
           can_save ? kTextU32 : kMutedU32, save_label);
  const ImVec2 save_pill{save_pos.x + action_size.x - 86.0F, save_pos.y + 32.0F};
  draw->AddRectFilled(save_pill, {save_pill.x + 68.0F, save_pill.y + 31.0F},
                      IM_COL32(255, 255, 255, 15), 7.0F);
  DrawText(draw, regular, 14.0F, {save_pill.x + 12.0F, save_pill.y + 7.0F},
           can_save ? IM_COL32(224, 213, 247, 255) : kMutedU32, "Alt + C");
  if (save_clicked && can_save && commands.save_clip) commands.save_clip();

  const bool record_disabled =
      snapshot.finalizing_recording ||
      (!snapshot.recording && snapshot.status != CaptureStatus::kBuffering);
  const ImVec2 record_pos{save_pos.x + action_size.x + action_gap, save_pos.y};
  ImGui::SetCursorScreenPos(record_pos);
  if (record_disabled) ImGui::BeginDisabled();
  ImGui::InvisibleButton("##record-card", action_size);
  const bool record_clicked = ImGui::IsItemClicked();
  const bool record_hovered = ImGui::IsItemHovered();
  if (record_disabled) ImGui::EndDisabled();
  DrawCard(draw, record_pos, action_size, record_hovered && !record_disabled,
           snapshot.recording ? IM_COL32(55, 24, 31, 255) : kPanel,
           snapshot.recording ? kRed : kPanelBorder, 13.0F);
  DrawRecordIcon(draw, {record_pos.x + 42.0F, record_pos.y + 47.0F},
                 snapshot.recording ? kRed : kTextU32);
  const char* record_label = snapshot.recording ? "STOP RECORDING" : "START RECORDING";
  DrawText(draw, semibold, 17.0F, {record_pos.x + 75.0F, record_pos.y + 36.0F},
           record_disabled ? kMutedU32 : kTextU32, record_label);
  const ImVec2 record_pill{record_pos.x + action_size.x - 86.0F, record_pos.y + 32.0F};
  draw->AddRectFilled(record_pill, {record_pill.x + 68.0F, record_pill.y + 31.0F},
                      IM_COL32(255, 255, 255, 10), 7.0F);
  DrawText(draw, regular, 14.0F, {record_pill.x + 12.0F, record_pill.y + 7.0F}, kMutedU32,
           "Alt + R");
  if (record_clicked && !record_disabled && commands.toggle_recording)
    commands.toggle_recording();
  if (snapshot.recording) {
    char recording_time[32]{};
    std::snprintf(recording_time, sizeof(recording_time), "REC  %02d:%02d",
                  static_cast<int>(snapshot.recording_seconds) / 60,
                  static_cast<int>(snapshot.recording_seconds) % 60);
    DrawText(draw, regular, 12.0F, {record_pos.x + 76.0F, record_pos.y + 61.0F}, kRed,
             recording_time);
  } else if (snapshot.finalizing_recording) {
    DrawText(draw, regular, 12.0F, {record_pos.x + 76.0F, record_pos.y + 61.0F}, kMutedU32,
             "FINALIZING...");
  }

  const float section_line_y = window_min.y + 247.0F;
  draw->AddLine({window_min.x + 1.0F, section_line_y}, {window_max.x - 1.0F, section_line_y},
                kDivider, 1.0F);
  DrawText(draw, semibold, 14.0F, {content.x, section_line_y + 24.0F}, kMutedU32,
           "CAPTURE SOURCE");

  const bool source_disabled = snapshot.recording || snapshot.finalizing_recording ||
                               snapshot.status == CaptureStatus::kSaving;
  int mode = snapshot.target_mode == CaptureTargetMode::kDisplay ? 1 : 0;
  constexpr const char* modes[] = {"Game / Window", "Display"};
  const float capture_y = section_line_y + 52.0F;
  const float capture_gap = 20.0F;
  const ImVec2 capture_size{(content_width - capture_gap) * 0.5F, 78.0F};
  const ImVec2 mode_pos{content.x, capture_y};
  ImGui::SetCursorScreenPos(mode_pos);
  if (source_disabled) ImGui::BeginDisabled();
  ImGui::InvisibleButton("##mode-card", capture_size);
  const bool mode_clicked = ImGui::IsItemClicked();
  const bool mode_hovered = ImGui::IsItemHovered();
  if (source_disabled) ImGui::EndDisabled();
  if (mode_clicked && !source_disabled) ImGui::OpenPopup("capture-mode-popup");
  DrawCard(draw, mode_pos, capture_size, mode_hovered && !source_disabled);
  DrawMonitorIcon(draw, {mode_pos.x + 42.0F, mode_pos.y + 39.0F},
                  source_disabled ? kMutedU32 : IM_COL32(202, 207, 218, 255));
  DrawText(draw, regular, 12.0F, {mode_pos.x + 84.0F, mode_pos.y + 21.0F}, kMutedU32,
           "MODE");
  DrawText(draw, semibold, 17.0F, {mode_pos.x + 84.0F, mode_pos.y + 41.0F},
           source_disabled ? kMutedU32 : kTextU32, modes[mode]);
  DrawChevron(draw, {mode_pos.x + capture_size.x - 34.0F, mode_pos.y + 39.0F}, kMutedU32);
  ImGui::SetNextWindowPos({mode_pos.x, mode_pos.y + capture_size.y + 8.0F},
                          ImGuiCond_Appearing);
  ImGui::SetNextWindowSize({capture_size.x, 108.0F}, ImGuiCond_Appearing);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0F, 8.0F});
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0F);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0.055F, 0.075F, 0.105F, 1.0F});
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.35F, 0.22F, 0.49F, 1.0F});
  if (ImGui::BeginPopup("capture-mode-popup", ImGuiWindowFlags_NoTitleBar)) {
    for (int option = 0; option < IM_ARRAYSIZE(modes); ++option) {
      ImGui::PushID(option);
      if (PopupOption("##mode-option", modes[option], option == mode) &&
          commands.set_target_mode) {
        commands.set_target_mode(option == 1 ? CaptureTargetMode::kDisplay
                                              : CaptureTargetMode::kGameWindow);
        ImGui::CloseCurrentPopup();
      }
      ImGui::PopID();
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);

  const auto selected_mode = mode == 1 ? CaptureTargetMode::kDisplay
                                       : CaptureTargetMode::kGameWindow;
  const auto& sources = mode == 1 ? snapshot.display_sources : snapshot.game_sources;
  const char* selected_label = sources.empty() ? "No compatible source found" : "Choose a source";
  for (const auto& source : sources) {
    if (source.id == snapshot.selected_capture_source_id) {
      selected_label = source.label.c_str();
      break;
    }
  }
  const ImVec2 source_pos{mode_pos.x + capture_size.x + capture_gap, capture_y};
  ImGui::SetCursorScreenPos(source_pos);
  if (source_disabled) ImGui::BeginDisabled();
  ImGui::InvisibleButton("##source-card", capture_size);
  const bool source_clicked = ImGui::IsItemClicked();
  const bool source_hovered = ImGui::IsItemHovered();
  if (source_disabled) ImGui::EndDisabled();
  if (source_clicked && !source_disabled && !sources.empty())
    ImGui::OpenPopup("capture-source-popup");
  DrawCard(draw, source_pos, capture_size, source_hovered && !source_disabled);
  DrawControllerIcon(draw, {source_pos.x + 42.0F, source_pos.y + 39.0F},
                     source_disabled ? kMutedU32 : IM_COL32(202, 207, 218, 255));
  DrawText(draw, regular, 12.0F, {source_pos.x + 84.0F, source_pos.y + 21.0F}, kMutedU32,
           "SOURCE");
  const auto compact_source = Ellipsize(
      semibold, 17.0F, CompactSourceLabel(selected_label), capture_size.x - 140.0F);
  DrawText(draw, semibold, 17.0F, {source_pos.x + 84.0F, source_pos.y + 41.0F},
           source_disabled ? kMutedU32 : kTextU32, compact_source);
  DrawChevron(draw, {source_pos.x + capture_size.x - 34.0F, source_pos.y + 39.0F}, kMutedU32);
  const float source_popup_height =
      std::clamp(18.0F + static_cast<float>(sources.size()) * 42.0F, 60.0F, 286.0F);
  ImGui::SetNextWindowPos({source_pos.x, source_pos.y + capture_size.y + 8.0F},
                          ImGuiCond_Appearing);
  ImGui::SetNextWindowSize({capture_size.x, source_popup_height}, ImGuiCond_Appearing);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0F, 8.0F});
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0F);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0.055F, 0.075F, 0.105F, 1.0F});
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.35F, 0.22F, 0.49F, 1.0F});
  if (ImGui::BeginPopup("capture-source-popup", ImGuiWindowFlags_NoTitleBar)) {
    for (const auto& source : sources) {
      const bool selected = source.id == snapshot.selected_capture_source_id;
      ImGui::PushID(static_cast<int>(source.id));
      const auto option_label = Ellipsize(semibold, 14.0F, CompactSourceLabel(source.label),
                                          capture_size.x - 70.0F);
      if (PopupOption("##source-option", option_label, selected) &&
          commands.select_capture_source) {
        commands.select_capture_source(selected_mode, source.id, source.label);
        ImGui::CloseCurrentPopup();
      }
      ImGui::PopID();
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);

  const float audio_title_y = capture_y + capture_size.y + 22.0F;
  DrawText(draw, semibold, 14.0F, {content.x, audio_title_y}, kMutedU32, "AUDIO");
  constexpr float audio_row_height = 52.0F;
  constexpr float audio_row_gap = 6.0F;
  const float audio_row_y = audio_title_y + 27.0F;
  const ImVec2 audio_row_size{content_width, audio_row_height};
  const float level_x = content.x + 265.0F;
  const float gain_width = content_width - 585.0F;
  DrawCard(draw, {content.x, audio_row_y}, audio_row_size, false, kPanel, kPanelBorder, 10.0F);
  DrawSpeakerIcon(draw, {content.x + 38.0F, audio_row_y + 26.0F}, kMutedU32);
  DrawText(draw, semibold, 13.0F, {content.x + 76.0F, audio_row_y + 18.0F}, kMutedU32,
           "DESKTOP AUDIO");
  float desktop_gain = static_cast<float>(config.desktop_audio_gain);
  if (GainSlider("##desktop-gain", {level_x, audio_row_y + 15.0F}, gain_width, desktop_gain) &&
      commands.set_desktop_audio_gain) {
    desktop_gain_dirty_ = true;
    commands.set_desktop_audio_gain(desktop_gain);
  }
  char desktop_gain_text[16]{};
  std::snprintf(desktop_gain_text, sizeof(desktop_gain_text), "%d%%",
                static_cast<int>(desktop_gain * 100.0F + 0.5F));
  DrawText(draw, semibold, 12.0F, {level_x + gain_width + 15.0F, audio_row_y + 18.0F},
           kTextU32, desktop_gain_text);
  const float meter_x = content.x + content_width - 208.0F;
  const int lit_segments = static_cast<int>(std::clamp(snapshot.desktop_audio_level, 0.0F, 1.0F) *
                                            18.0F);
  for (int segment = 0; segment < 18; ++segment) {
    const ImU32 meter_color = segment < lit_segments ? kGreen : IM_COL32(32, 73, 55, 255);
    draw->AddRectFilled({meter_x + segment * 7.0F, audio_row_y + 22.0F},
                        {meter_x + segment * 7.0F + 4.0F, audio_row_y + 30.0F}, meter_color);
  }
  DrawText(draw, semibold, 13.0F, {content.x + content_width - 59.0F, audio_row_y + 18.0F},
           snapshot.desktop_audio_active ? kGreen : kMutedU32,
           snapshot.desktop_audio_active ? "LIVE" : "QUIET");

  const float microphone_y = audio_row_y + audio_row_height + audio_row_gap;
  DrawCard(draw, {content.x, microphone_y}, audio_row_size, false, kPanel, kPanelBorder, 10.0F);
  DrawMicrophoneIcon(draw, {content.x + 38.0F, microphone_y + 24.0F}, kMutedU32);
  DrawText(draw, semibold, 13.0F, {content.x + 76.0F, microphone_y + 18.0F}, kMutedU32,
           "MICROPHONE");
  bool microphone_enabled = snapshot.microphone_enabled;
  float microphone_gain = static_cast<float>(config.microphone_audio_gain);
  if (GainSlider("##microphone-gain", {level_x, microphone_y + 15.0F}, gain_width,
                 microphone_gain) &&
      commands.set_microphone_audio_gain) {
    microphone_gain_dirty_ = true;
    commands.set_microphone_audio_gain(microphone_gain);
  }
  char microphone_gain_text[16]{};
  std::snprintf(microphone_gain_text, sizeof(microphone_gain_text), "%d%%",
                static_cast<int>(microphone_gain * 100.0F + 0.5F));
  DrawText(draw, semibold, 12.0F, {level_x + gain_width + 15.0F, microphone_y + 18.0F},
           microphone_enabled ? kTextU32 : kMutedU32, microphone_gain_text);
  if (Switch("##microphone-toggle",
             {content.x + content_width - 70.0F, microphone_y + 13.0F}, microphone_enabled) &&
      commands.set_microphone_enabled) {
    microphone_enabled = !microphone_enabled;
    commands.set_microphone_enabled(microphone_enabled);
  }
  if ((desktop_gain_dirty_ || microphone_gain_dirty_) &&
      !ImGui::IsMouseDown(ImGuiMouseButton_Left) && commands.persist_audio_gains) {
    commands.persist_audio_gains();
    desktop_gain_dirty_ = false;
    microphone_gain_dirty_ = false;
  }

  const char* selected_microphone = "No microphone detected";
  if (snapshot.selected_microphone_index >= 0 &&
      snapshot.selected_microphone_index < static_cast<int>(snapshot.microphones.size()))
    selected_microphone = snapshot.microphones[static_cast<std::size_t>(
        snapshot.selected_microphone_index)].c_str();
  const float input_y = microphone_y + audio_row_height + audio_row_gap;
  const ImVec2 input_pos{content.x, input_y};
  ImGui::SetCursorScreenPos(input_pos);
  ImGui::InvisibleButton("##microphone-input-card", audio_row_size);
  const bool input_clicked = ImGui::IsItemClicked();
  const bool input_hovered = ImGui::IsItemHovered();
  if (input_clicked && !snapshot.microphones.empty()) ImGui::OpenPopup("microphone-popup");
  DrawCard(draw, input_pos, audio_row_size, input_hovered, kPanel, kPanelBorder, 10.0F);
  DrawMicrophoneIcon(draw, {content.x + 38.0F, input_y + 24.0F}, kPurpleBright);
  std::string_view microphone_name = selected_microphone;
  constexpr std::string_view microphone_prefix = "Microphone (";
  if (microphone_name.starts_with(microphone_prefix) && microphone_name.ends_with(')')) {
    microphone_name.remove_prefix(microphone_prefix.size());
    microphone_name.remove_suffix(1);
  }
  std::string mic_label = "MICROPHONE (";
  mic_label += microphone_name;
  mic_label += ')';
  DrawText(draw, semibold, 13.0F, {content.x + 76.0F, input_y + 18.0F}, kMutedU32,
           mic_label);
  DrawProgress(draw, {content.x + 420.0F, input_y + 23.0F},
               {content_width - 660.0F, 6.0F}, snapshot.microphone_level, kGreen);
  DrawText(draw, regular, 11.0F,
           {content.x + content_width - 202.0F, input_y + 10.0F}, kMutedU32, "INPUT");
  DrawText(draw, semibold, 15.0F,
           {content.x + content_width - 202.0F, input_y + 28.0F}, kTextU32, "Mic");
  DrawChevron(draw, {content.x + content_width - 34.0F, input_y + 26.0F}, kMutedU32);
  const float microphone_popup_height =
      std::clamp(18.0F + static_cast<float>(snapshot.microphones.size()) * 42.0F, 60.0F,
                 244.0F);
  ImGui::SetNextWindowPos({input_pos.x, input_pos.y + audio_row_size.y + 8.0F},
                          ImGuiCond_Appearing);
  ImGui::SetNextWindowSize({audio_row_size.x, microphone_popup_height}, ImGuiCond_Appearing);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0F, 8.0F});
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0F);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0.055F, 0.075F, 0.105F, 1.0F});
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.35F, 0.22F, 0.49F, 1.0F});
  if (ImGui::BeginPopup("microphone-popup", ImGuiWindowFlags_NoTitleBar)) {
    for (int index = 0; index < static_cast<int>(snapshot.microphones.size()); ++index) {
      const bool selected = index == snapshot.selected_microphone_index;
      ImGui::PushID(index);
      const auto microphone_option = Ellipsize(
          semibold, 14.0F, snapshot.microphones[static_cast<std::size_t>(index)],
          audio_row_size.x - 70.0F);
      if (PopupOption("##microphone-option", microphone_option, selected) &&
          commands.select_microphone) {
        commands.select_microphone(index);
        ImGui::CloseCurrentPopup();
      }
      ImGui::PopID();
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);

  const float system_line_y = input_y + audio_row_height + 20.0F;
  draw->AddLine({window_min.x + 1.0F, system_line_y}, {window_max.x - 1.0F, system_line_y},
                kDivider, 1.0F);
  DrawText(draw, semibold, 14.0F, {content.x, system_line_y + 21.0F}, kMutedU32,
           "SYSTEM STATUS");
  const float metric_y = system_line_y + 49.0F;
  constexpr float metric_width = 190.0F;
  const char* metric_labels[] = {"FPS", "ENCODER", "BUFFER RAM", "DROPPED"};
  char fps_value[24]{};
  char buffer_value[24]{};
  char dropped_value[24]{};
  std::snprintf(fps_value, sizeof(fps_value), "%.1f", snapshot.metrics.capture_fps);
  std::snprintf(buffer_value, sizeof(buffer_value), "%.0f MiB",
                static_cast<double>(snapshot.metrics.rolling_buffer_bytes) / (1024.0 * 1024.0));
  std::snprintf(dropped_value, sizeof(dropped_value), "%llu",
                static_cast<unsigned long long>(snapshot.metrics.dropped_raw_frames +
                                                snapshot.metrics.dropped_encode_frames));
  const char* metric_values[] = {fps_value,
                                 snapshot.selected_encoder.empty()
                                     ? "warming up"
                                     : snapshot.selected_encoder.c_str(),
                                 buffer_value, dropped_value};
  for (int metric = 0; metric < 4; ++metric) {
    const float x = content.x + metric * metric_width;
    DrawText(draw, regular, 12.0F, {x, metric_y}, kMutedU32, metric_labels[metric]);
    DrawText(draw, semibold, 20.0F, {x, metric_y + 24.0F}, kTextU32, metric_values[metric]);
    if (metric < 3)
      draw->AddLine({x + metric_width - 26.0F, metric_y - 4.0F},
                    {x + metric_width - 26.0F, metric_y + 56.0F}, kDivider, 1.0F);
  }

  const ImVec2 folder_pos{content.x + content_width - 250.0F, metric_y - 4.0F};
  const ImVec2 folder_size{250.0F, 52.0F};
  ImGui::SetCursorScreenPos(folder_pos);
  ImGui::InvisibleButton("##open-clips-folder", folder_size);
  const bool folder_clicked = ImGui::IsItemClicked();
  const bool folder_hovered = ImGui::IsItemHovered();
  DrawCard(draw, folder_pos, folder_size, folder_hovered, IM_COL32(24, 22, 37, 255),
           kPurpleBright, 10.0F);
  DrawFolderIcon(draw, {folder_pos.x + 34.0F, folder_pos.y + 26.0F}, kPurpleBright);
  DrawText(draw, semibold, 14.0F, {folder_pos.x + 59.0F, folder_pos.y + 17.0F},
           kPurpleBright, "OPEN CLIPS FOLDER");
  if (folder_clicked && commands.open_output_folder) commands.open_output_folder();

  const char* hotkey_copy = hotkeys_available
                                ? "Alt + R   Record     Alt + C   Clip     Alt + X   Hide"
                                : "Global hotkeys unavailable";
  const float hotkey_width = TextWidth(regular, 12.0F, hotkey_copy);
  DrawText(draw, regular, 12.0F,
           {content.x + content_width - hotkey_width, metric_y + 60.0F}, kMutedU32,
           hotkey_copy);

  if (snapshot.last_error.has_value()) {
    std::string issue = "ISSUE: " + snapshot.last_error->ToString();
    if (issue.size() > 88) issue.resize(88);
    DrawText(draw, regular, 11.0F, {content.x, metric_y + 62.0F}, kRed, issue);
  }

  const float footer_y = window_max.y - 53.0F;
  draw->AddLine({window_min.x + 1.0F, footer_y}, {window_max.x - 1.0F, footer_y}, kDivider,
                1.0F);
  const ImVec2 settings_pos{content.x - 10.0F, footer_y + 9.0F};
  ImGui::SetCursorScreenPos(settings_pos);
  ImGui::InvisibleButton("##settings-footer", {118.0F, 34.0F});
  const bool settings_clicked = ImGui::IsItemClicked();
  const bool settings_hovered = ImGui::IsItemHovered();
  if (settings_hovered)
    draw->AddRectFilled(settings_pos, {settings_pos.x + 118.0F, settings_pos.y + 34.0F},
                        IM_COL32(255, 255, 255, 7), 7.0F);
  const ImVec2 gear_center{settings_pos.x + 18.0F, settings_pos.y + 17.0F};
  draw->AddCircle(gear_center, 7.0F, kMutedU32, 16, 2.0F);
  draw->AddCircle(gear_center, 2.5F, kMutedU32, 12, 1.5F);
  for (int spoke = 0; spoke < 4; ++spoke) {
    const float horizontal = spoke % 2 == 0 ? 1.0F : 0.0F;
    const float vertical = spoke % 2 == 0 ? 0.0F : 1.0F;
    const float direction = spoke < 2 ? -1.0F : 1.0F;
    draw->AddLine({gear_center.x + horizontal * direction * 7.0F,
                   gear_center.y + vertical * direction * 7.0F},
                  {gear_center.x + horizontal * direction * 10.0F,
                   gear_center.y + vertical * direction * 10.0F},
                  kMutedU32, 2.0F);
  }
  DrawText(draw, regular, 13.0F, {settings_pos.x + 38.0F, settings_pos.y + 9.0F},
           kMutedU32, "Settings");
  if (settings_clicked) {
    ResetDraft(config);
    settings_open_ = true;
  }
  constexpr std::string_view version = "v0.3.0";
  DrawText(draw, regular, 12.0F,
           {content.x + content_width - TextWidth(regular, 12.0F, version), footer_y + 20.0F},
           kMutedU32, version);
}

void MainPanel::RenderSettings(const ApplicationSnapshot& snapshot, const AppConfig& config,
                               const UiCommands& commands) {
  if (!draft_initialized_) ResetDraft(config);
  ImGui::TextColored(kLavender, "KLIP");
  ImGui::SameLine();
  ImGui::TextColored(kMuted, " / SETTINGS");
  ImGui::SameLine(ImGui::GetWindowWidth() - 72.0F);
  if (ImGui::SmallButton("BACK")) settings_open_ = false;
  ImGui::TextColored(kMuted,
                     "Hardware-first defaults keep capture light. Saved changes apply on restart.");

  SectionLabel("VIDEO");
  int fps = draft_.target_fps == 30 ? 0 : 1;
  constexpr const char* frame_rates[] = {"30 FPS - lightest", "60 FPS - recommended"};
  if (ImGui::Combo("Frame rate", &fps, frame_rates, IM_ARRAYSIZE(frame_rates)))
    draft_.target_fps = fps == 0 ? 30U : 60U;

  int resolution = draft_.output_width == 1280 ? 2 : draft_.output_width == 1920 ? 1 : 0;
  constexpr const char* resolutions[] = {"Source resolution", "1920 x 1080", "1280 x 720"};
  if (ImGui::Combo("Output size", &resolution, resolutions, IM_ARRAYSIZE(resolutions))) {
    draft_.output_width = resolution == 1 ? 1920U : resolution == 2 ? 1280U : 0U;
    draft_.output_height = resolution == 1 ? 1080U : resolution == 2 ? 720U : 0U;
  }

  int bitrate_mbps = static_cast<int>(draft_.video_bitrate / 1'000'000);
  if (ImGui::SliderInt("Video bitrate", &bitrate_mbps, 4, 40, "%d Mbps")) {
    draft_.video_bitrate = static_cast<std::int64_t>(bitrate_mbps) * 1'000'000;
    ResizeReplayBudget(draft_);
  }
  int encoder = EncoderProfile(draft_.encoder_preferences);
  constexpr const char* encoders[] = {"Auto - recommended", "NVIDIA NVENC", "AMD AMF",
                                       "Windows hardware (Intel + fallback)"};
  if (ImGui::Combo("Encoder", &encoder, encoders, IM_ARRAYSIZE(encoders)))
    SetEncoderProfile(draft_, encoder);
  int quality = static_cast<int>(draft_.encoder_quality);
  constexpr const char* qualities[] = {"Performance", "Balanced", "Quality"};
  if (ImGui::Combo("Encoder load", &quality, qualities, IM_ARRAYSIZE(qualities)))
    draft_.encoder_quality = static_cast<EncoderQuality>(quality);
  ImGui::Checkbox("Capture mouse cursor", &draft_.capture_cursor);

  SectionLabel("CLIPS AND AUDIO");
  int clip_seconds = static_cast<int>(draft_.clip_duration_seconds);
  if (ImGui::SliderInt("Replay length", &clip_seconds, 15, 300, "%d seconds")) {
    draft_.clip_duration_seconds = static_cast<double>(clip_seconds);
    ResizeReplayBudget(draft_);
  }
  int audio_bitrate = static_cast<int>(draft_.audio_bitrate / 1000);
  if (ImGui::SliderInt("Audio bitrate", &audio_bitrate, 96, 320, "%d kbps")) {
    draft_.audio_bitrate = static_cast<std::int64_t>(audio_bitrate) * 1000;
    ResizeReplayBudget(draft_);
  }
  int desktop_volume = static_cast<int>(draft_.desktop_audio_gain * 100.0 + 0.5);
  if (ImGui::SliderInt("Desktop volume", &desktop_volume, 0, 200, "%d%%"))
    draft_.desktop_audio_gain = static_cast<double>(desktop_volume) / 100.0;
  int microphone_volume = static_cast<int>(draft_.microphone_audio_gain * 100.0 + 0.5);
  if (ImGui::SliderInt("Microphone volume", &microphone_volume, 0, 200, "%d%%"))
    draft_.microphone_audio_gain = static_cast<double>(microphone_volume) / 100.0;
  ImGui::Checkbox("Enable microphone (voice / keyboard clicks) on launch",
                  &draft_.microphone_enabled);

  SectionLabel("STORAGE");
  ImGui::InputText("Clips folder", clips_path_.data(), clips_path_.size());
  ImGui::InputText("Recordings folder", recordings_path_.data(), recordings_path_.size());
  ImGui::TextColored(kMuted,
                     "Replay RAM is capped at %.0f MiB. Encoded packets are shared, not duplicated.",
                     static_cast<double>(draft_.rolling_buffer_bytes) / (1024.0 * 1024.0));

  SectionLabel("SAVE");
  if (ImGui::Button("SAVE SETTINGS", {-1.0F, 42.0F}) && commands.save_settings) {
    draft_.output_directory = ParsePath(clips_path_.data());
    draft_.recording_directory = ParsePath(recordings_path_.data());
    commands.save_settings(draft_);
  }
  if (!snapshot.settings_message.empty())
    ImGui::TextColored(snapshot.settings_restart_required ? ImVec4{1.0F, 0.73F, 0.25F, 1.0F}
                                                         : kMint,
                       "%s", snapshot.settings_message.c_str());
  ImGui::TextWrapped("Capture mode, source selection, and audio volume apply immediately. Video, "
                     "encoder, buffer, and storage changes apply after restarting Klip.");
}

void MainPanel::ResetDraft(const AppConfig& config) {
  draft_ = config;
  const auto clips = PathText(config.output_directory);
  const auto recordings = PathText(config.recording_directory);
  std::snprintf(clips_path_.data(), clips_path_.size(), "%s", clips.c_str());
  std::snprintf(recordings_path_.data(), recordings_path_.size(), "%s", recordings.c_str());
  draft_initialized_ = true;
}

}  // namespace klip
