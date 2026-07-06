#include <string_view>

namespace json_tests {
inline constexpr char detail_fail31_bytes[] {
'\x5b','\x30','\x65','\x2b','\x2d','\x31','\x5d',
};

inline constexpr std::string_view fail31{std::begin(detail_fail31_bytes), std::end(detail_fail31_bytes)};
}
