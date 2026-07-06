#include <string_view>

namespace json_tests {
inline constexpr char detail_fail1_bytes[] {
'\x22','\x54','\x68','\x69','\x73','\x20','\x69','\x73',
'\x20','\x61','\x20','\x73','\x74','\x72','\x69','\x6e',
'\x67','\x20','\x74','\x68','\x61','\x74','\x20','\x6e',
'\x65','\x76','\x65','\x72','\x20','\x65','\x6e','\x64',
'\x73','\x2c','\x20','\x79','\x65','\x73','\x20','\x69',
'\x74','\x20','\x67','\x6f','\x65','\x73','\x20','\x6f',
'\x6e','\x20','\x61','\x6e','\x64','\x20','\x6f','\x6e',
'\x2c','\x20','\x6d','\x79','\x20','\x66','\x72','\x69',
'\x65','\x6e','\x64','\x73','\x2e','\x0a',
};

inline constexpr std::string_view fail1{std::begin(detail_fail1_bytes), std::end(detail_fail1_bytes)};
}
