#include <string_view>

namespace json_tests {
inline constexpr char detail_fail30_bytes[] {
'\x5b','\x30','\x65','\x2b','\x5d',
};

inline constexpr std::string_view fail30{std::begin(detail_fail30_bytes), std::end(detail_fail30_bytes)};
}
