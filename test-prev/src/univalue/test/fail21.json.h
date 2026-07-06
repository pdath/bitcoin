#include <string_view>

namespace json_tests {
inline constexpr char detail_fail21_bytes[] {
'\x7b','\x22','\x43','\x6f','\x6d','\x6d','\x61','\x20',
'\x69','\x6e','\x73','\x74','\x65','\x61','\x64','\x20',
'\x6f','\x66','\x20','\x63','\x6f','\x6c','\x6f','\x6e',
'\x22','\x2c','\x20','\x6e','\x75','\x6c','\x6c','\x7d',

};

inline constexpr std::string_view fail21{std::begin(detail_fail21_bytes), std::end(detail_fail21_bytes)};
}
