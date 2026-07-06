#include <string_view>

namespace json_tests {
inline constexpr char detail_fail39_bytes[] {
'\x5b','\x22','\x5c','\x75','\x64','\x64','\x36','\x31',
'\x22','\x5d','\x0a',
};

inline constexpr std::string_view fail39{std::begin(detail_fail39_bytes), std::end(detail_fail39_bytes)};
}
