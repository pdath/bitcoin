#include <string_view>

namespace json_tests {
inline constexpr char detail_fail42_bytes[] {
'\x5b','\x22','\x62','\x65','\x66','\x6f','\x72','\x65',
'\x20','\x6e','\x75','\x6c','\x20','\x62','\x79','\x74',
'\x65','\x22','\x5d','\x00','\x22','\x61','\x66','\x74',
'\x65','\x72','\x20','\x6e','\x75','\x6c','\x20','\x62',
'\x79','\x74','\x65','\x22','\x0a',
};

inline constexpr std::string_view fail42{std::begin(detail_fail42_bytes), std::end(detail_fail42_bytes)};
}
