#include <string_view>

namespace json_tests {
inline constexpr char detail_round4_bytes[] {
'\x37','\x0a',
};

inline constexpr std::string_view round4{std::begin(detail_round4_bytes), std::end(detail_round4_bytes)};
}
