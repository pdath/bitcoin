#include <string_view>

namespace json_tests {
inline constexpr char detail_fail2_bytes[] {
'\x5b','\x22','\x55','\x6e','\x63','\x6c','\x6f','\x73',
'\x65','\x64','\x20','\x61','\x72','\x72','\x61','\x79',
'\x22',
};

inline constexpr std::string_view fail2{std::begin(detail_fail2_bytes), std::end(detail_fail2_bytes)};
}
