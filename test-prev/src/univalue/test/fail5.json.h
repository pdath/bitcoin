#include <string_view>

namespace json_tests {
inline constexpr char detail_fail5_bytes[] {
'\x5b','\x22','\x64','\x6f','\x75','\x62','\x6c','\x65',
'\x20','\x65','\x78','\x74','\x72','\x61','\x20','\x63',
'\x6f','\x6d','\x6d','\x61','\x22','\x2c','\x2c','\x5d',

};

inline constexpr std::string_view fail5{std::begin(detail_fail5_bytes), std::end(detail_fail5_bytes)};
}
