#include <string_view>

namespace json_tests {
inline constexpr char detail_fail24_bytes[] {
'\x5b','\x27','\x73','\x69','\x6e','\x67','\x6c','\x65',
'\x20','\x71','\x75','\x6f','\x74','\x65','\x27','\x5d',

};

inline constexpr std::string_view fail24{std::begin(detail_fail24_bytes), std::end(detail_fail24_bytes)};
}
