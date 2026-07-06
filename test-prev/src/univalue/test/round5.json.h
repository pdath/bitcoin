#include <string_view>

namespace json_tests {
inline constexpr char detail_round5_bytes[] {
'\x74','\x72','\x75','\x65','\x0a',
};

inline constexpr std::string_view round5{std::begin(detail_round5_bytes), std::end(detail_round5_bytes)};
}
