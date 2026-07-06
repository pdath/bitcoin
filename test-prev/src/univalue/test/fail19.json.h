#include <string_view>

namespace json_tests {
inline constexpr char detail_fail19_bytes[] {
'\x7b','\x22','\x4d','\x69','\x73','\x73','\x69','\x6e',
'\x67','\x20','\x63','\x6f','\x6c','\x6f','\x6e','\x22',
'\x20','\x6e','\x75','\x6c','\x6c','\x7d',
};

inline constexpr std::string_view fail19{std::begin(detail_fail19_bytes), std::end(detail_fail19_bytes)};
}
