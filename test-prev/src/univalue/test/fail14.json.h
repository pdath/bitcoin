#include <string_view>

namespace json_tests {
inline constexpr char detail_fail14_bytes[] {
'\x7b','\x22','\x4e','\x75','\x6d','\x62','\x65','\x72',
'\x73','\x20','\x63','\x61','\x6e','\x6e','\x6f','\x74',
'\x20','\x62','\x65','\x20','\x68','\x65','\x78','\x22',
'\x3a','\x20','\x30','\x78','\x31','\x34','\x7d',
};

inline constexpr std::string_view fail14{std::begin(detail_fail14_bytes), std::end(detail_fail14_bytes)};
}
