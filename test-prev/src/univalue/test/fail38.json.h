#include <string_view>

namespace json_tests {
inline constexpr char detail_fail38_bytes[] {
'\x5b','\x22','\x5c','\x75','\x64','\x38','\x33','\x34',
'\x22','\x5d','\x0a',
};

inline constexpr std::string_view fail38{std::begin(detail_fail38_bytes), std::end(detail_fail38_bytes)};
}
