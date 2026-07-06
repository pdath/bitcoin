#include <string_view>

namespace json_tests {
inline constexpr char detail_fail13_bytes[] {
'\x7b','\x22','\x4e','\x75','\x6d','\x62','\x65','\x72',
'\x73','\x20','\x63','\x61','\x6e','\x6e','\x6f','\x74',
'\x20','\x68','\x61','\x76','\x65','\x20','\x6c','\x65',
'\x61','\x64','\x69','\x6e','\x67','\x20','\x7a','\x65',
'\x72','\x6f','\x65','\x73','\x22','\x3a','\x20','\x30',
'\x31','\x33','\x7d',
};

inline constexpr std::string_view fail13{std::begin(detail_fail13_bytes), std::end(detail_fail13_bytes)};
}
