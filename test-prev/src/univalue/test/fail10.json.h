#include <string_view>

namespace json_tests {
inline constexpr char detail_fail10_bytes[] {
'\x7b','\x22','\x45','\x78','\x74','\x72','\x61','\x20',
'\x76','\x61','\x6c','\x75','\x65','\x20','\x61','\x66',
'\x74','\x65','\x72','\x20','\x63','\x6c','\x6f','\x73',
'\x65','\x22','\x3a','\x20','\x74','\x72','\x75','\x65',
'\x7d','\x20','\x22','\x6d','\x69','\x73','\x70','\x6c',
'\x61','\x63','\x65','\x64','\x20','\x71','\x75','\x6f',
'\x74','\x65','\x64','\x20','\x76','\x61','\x6c','\x75',
'\x65','\x22',
};

inline constexpr std::string_view fail10{std::begin(detail_fail10_bytes), std::end(detail_fail10_bytes)};
}
