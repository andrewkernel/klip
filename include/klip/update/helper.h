#pragma once
#include <functional>
namespace klip {
int RunUpdateHelper(int argc, wchar_t** argv,
                    std::function<void()> report_failure = {});
}
