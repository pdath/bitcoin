#include <string_view>

namespace json_tests {
inline constexpr char detail_fail3_bytes[] {
'\x7b','\x75','\x6e','\x71','\x75','\x6f','\x74','\x65',
'\x64','\x5f','\x6b','\x65','\x79','\x3a','\x20','\x22',
'\x6b','\x65','\x79','\x73','\x20','\x6d','\x75','\x73',
'\x74','\x20','\x62','\x65','\x20','\x71','\x75','\x6f',
'\x74','\x65','\x64','\x22','\x7d',
};

inline constexpr std::string_view fail3{std::begin(detail_fail3_bytes), std::end(detail_fail3_bytes)};
}
