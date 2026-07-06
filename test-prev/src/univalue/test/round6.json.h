#include <string_view>

namespace json_tests {
inline constexpr char detail_round6_bytes[] {
'\x66','\x61','\x6c','\x73','\x65','\x0a',
};

inline constexpr std::string_view round6{std::begin(detail_round6_bytes), std::end(detail_round6_bytes)};
}
