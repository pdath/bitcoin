#include <string_view>

namespace json_tests {
inline constexpr char detail_fail8_bytes[] {
'\x5b','\x22','\x45','\x78','\x74','\x72','\x61','\x20',
'\x63','\x6c','\x6f','\x73','\x65','\x22','\x5d','\x5d',

};

inline constexpr std::string_view fail8{std::begin(detail_fail8_bytes), std::end(detail_fail8_bytes)};
}
