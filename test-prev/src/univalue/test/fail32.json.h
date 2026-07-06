#include <string_view>

namespace json_tests {
inline constexpr char detail_fail32_bytes[] {
'\x7b','\x22','\x43','\x6f','\x6d','\x6d','\x61','\x20',
'\x69','\x6e','\x73','\x74','\x65','\x61','\x64','\x20',
'\x69','\x66','\x20','\x63','\x6c','\x6f','\x73','\x69',
'\x6e','\x67','\x20','\x62','\x72','\x61','\x63','\x65',
'\x22','\x3a','\x20','\x74','\x72','\x75','\x65','\x2c',

};

inline constexpr std::string_view fail32{std::begin(detail_fail32_bytes), std::end(detail_fail32_bytes)};
}
