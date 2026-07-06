#include <string_view>

namespace json_tests {
inline constexpr char detail_round7_bytes[] {
'\x6e','\x75','\x6c','\x6c','\x0a',
};

inline constexpr std::string_view round7{std::begin(detail_round7_bytes), std::end(detail_round7_bytes)};
}
