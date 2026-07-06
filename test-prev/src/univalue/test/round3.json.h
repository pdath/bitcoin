#include <string_view>

namespace json_tests {
inline constexpr char detail_round3_bytes[] {
'\x22','\x61','\x62','\x63','\x64','\x65','\x66','\x67',
'\x68','\x69','\x6a','\x6b','\x6c','\x6d','\x6e','\x6f',
'\x70','\x71','\x72','\x73','\x74','\x75','\x76','\x77',
'\x78','\x79','\x7a','\x22','\x0a',
};

inline constexpr std::string_view round3{std::begin(detail_round3_bytes), std::end(detail_round3_bytes)};
}
