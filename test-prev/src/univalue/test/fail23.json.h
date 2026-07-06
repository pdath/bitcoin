#include <string_view>

namespace json_tests {
inline constexpr char detail_fail23_bytes[] {
'\x5b','\x22','\x42','\x61','\x64','\x20','\x76','\x61',
'\x6c','\x75','\x65','\x22','\x2c','\x20','\x74','\x72',
'\x75','\x74','\x68','\x5d',
};

inline constexpr std::string_view fail23{std::begin(detail_fail23_bytes), std::end(detail_fail23_bytes)};
}
