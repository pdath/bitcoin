#include <string_view>

namespace json_tests {
inline constexpr char detail_fail33_bytes[] {
'\x5b','\x22','\x6d','\x69','\x73','\x6d','\x61','\x74',
'\x63','\x68','\x22','\x7d',
};

inline constexpr std::string_view fail33{std::begin(detail_fail33_bytes), std::end(detail_fail33_bytes)};
}
