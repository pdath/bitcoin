#include <string_view>

namespace json_tests {
inline constexpr char detail_fail16_bytes[] {
'\x5b','\x5c','\x6e','\x61','\x6b','\x65','\x64','\x5d',

};

inline constexpr std::string_view fail16{std::begin(detail_fail16_bytes), std::end(detail_fail16_bytes)};
}
