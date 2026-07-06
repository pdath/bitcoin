#include <string_view>

namespace json_tests {
inline constexpr char detail_fail26_bytes[] {
'\x5b','\x22','\x74','\x61','\x62','\x5c','\x20','\x20',
'\x20','\x63','\x68','\x61','\x72','\x61','\x63','\x74',
'\x65','\x72','\x5c','\x20','\x20','\x20','\x69','\x6e',
'\x5c','\x20','\x20','\x73','\x74','\x72','\x69','\x6e',
'\x67','\x5c','\x20','\x20','\x22','\x5d',
};

inline constexpr std::string_view fail26{std::begin(detail_fail26_bytes), std::end(detail_fail26_bytes)};
}
