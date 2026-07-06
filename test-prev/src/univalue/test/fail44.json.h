#include <string_view>

namespace json_tests {
inline constexpr char detail_fail44_bytes[] {
'\x22','\x54','\x68','\x69','\x73','\x20','\x66','\x69',
'\x6c','\x65','\x20','\x65','\x6e','\x64','\x73','\x20',
'\x77','\x69','\x74','\x68','\x6f','\x75','\x74','\x20',
'\x61','\x20','\x6e','\x65','\x77','\x6c','\x69','\x6e',
'\x65','\x20','\x6f','\x72','\x20','\x63','\x6c','\x6f',
'\x73','\x65','\x2d','\x71','\x75','\x6f','\x74','\x65',
'\x2e',
};

inline constexpr std::string_view fail44{std::begin(detail_fail44_bytes), std::end(detail_fail44_bytes)};
}
