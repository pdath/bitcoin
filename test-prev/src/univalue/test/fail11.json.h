#include <string_view>

namespace json_tests {
inline constexpr char detail_fail11_bytes[] {
'\x7b','\x22','\x49','\x6c','\x6c','\x65','\x67','\x61',
'\x6c','\x20','\x65','\x78','\x70','\x72','\x65','\x73',
'\x73','\x69','\x6f','\x6e','\x22','\x3a','\x20','\x31',
'\x20','\x2b','\x20','\x32','\x7d',
};

inline constexpr std::string_view fail11{std::begin(detail_fail11_bytes), std::end(detail_fail11_bytes)};
}
