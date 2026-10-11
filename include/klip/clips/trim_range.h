#pragma once
#include <vector>
namespace klip {
double TrimKeyframeStart(const std::vector<double>& keyframes,
                         double requested);
double TrimKeyframeEnd(const std::vector<double>& keyframes, double requested,
                       double duration);
bool ValidTrimRange(double start, double end, double duration);
}  // namespace klip
