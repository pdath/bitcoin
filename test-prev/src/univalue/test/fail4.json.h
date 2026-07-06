#include <string_view>

namespace json_tests {
inline constexpr char detail_fail4_bytes[] {
'\x5b','\x22','\x65','\x78','\x74','\x72','\x61','\x20',
'\x63','\x6f','\x6d','\x6d','\x61','\x22','\x2c','\x5d',

};

inline constexpr std::string_view fail4{std::begin(detail_fail4_bytes), std::end(detail_fail4_bytes)};
}
