#include <string_view>

namespace json_tests {
inline constexpr char detail_fail28_bytes[] {
'\x5b','\x22','\x6c','\x69','\x6e','\x65','\x5c','\x0a',
'\x62','\x72','\x65','\x61','\x6b','\x22','\x5d',
};

inline constexpr std::string_view fail28{std::begin(detail_fail28_bytes), std::end(detail_fail28_bytes)};
}
