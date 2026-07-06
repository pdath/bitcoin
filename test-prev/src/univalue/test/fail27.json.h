#include <string_view>

namespace json_tests {
inline constexpr char detail_fail27_bytes[] {
'\x5b','\x22','\x6c','\x69','\x6e','\x65','\x0a','\x62',
'\x72','\x65','\x61','\x6b','\x22','\x5d',
};

inline constexpr std::string_view fail27{std::begin(detail_fail27_bytes), std::end(detail_fail27_bytes)};
}
