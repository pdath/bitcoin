#include <string_view>

namespace json_tests {
inline constexpr char detail_fail12_bytes[] {
'\x7b','\x22','\x49','\x6c','\x6c','\x65','\x67','\x61',
'\x6c','\x20','\x69','\x6e','\x76','\x6f','\x63','\x61',
'\x74','\x69','\x6f','\x6e','\x22','\x3a','\x20','\x61',
'\x6c','\x65','\x72','\x74','\x28','\x29','\x7d',
};

inline constexpr std::string_view fail12{std::begin(detail_fail12_bytes), std::end(detail_fail12_bytes)};
}
