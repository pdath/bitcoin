#include <string_view>

namespace json_tests {
inline constexpr char detail_fail6_bytes[] {
'\x5b','\x20','\x20','\x20','\x2c','\x20','\x22','\x3c',
'\x2d','\x2d','\x20','\x6d','\x69','\x73','\x73','\x69',
'\x6e','\x67','\x20','\x76','\x61','\x6c','\x75','\x65',
'\x22','\x5d',
};

inline constexpr std::string_view fail6{std::begin(detail_fail6_bytes), std::end(detail_fail6_bytes)};
}
