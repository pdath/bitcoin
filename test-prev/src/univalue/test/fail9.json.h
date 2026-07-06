#include <string_view>

namespace json_tests {
inline constexpr char detail_fail9_bytes[] {
'\x7b','\x22','\x45','\x78','\x74','\x72','\x61','\x20',
'\x63','\x6f','\x6d','\x6d','\x61','\x22','\x3a','\x20',
'\x74','\x72','\x75','\x65','\x2c','\x7d',
};

inline constexpr std::string_view fail9{std::begin(detail_fail9_bytes), std::end(detail_fail9_bytes)};
}
