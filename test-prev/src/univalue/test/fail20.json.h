#include <string_view>

namespace json_tests {
inline constexpr char detail_fail20_bytes[] {
'\x7b','\x22','\x44','\x6f','\x75','\x62','\x6c','\x65',
'\x20','\x63','\x6f','\x6c','\x6f','\x6e','\x22','\x3a',
'\x3a','\x20','\x6e','\x75','\x6c','\x6c','\x7d',
};

inline constexpr std::string_view fail20{std::begin(detail_fail20_bytes), std::end(detail_fail20_bytes)};
}
