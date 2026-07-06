#include <string_view>

namespace json_tests {
inline constexpr char detail_fail35_bytes[] {
'\x5b','\x20','\x74','\x72','\x75','\x65','\x20','\x74',
'\x72','\x75','\x65','\x20','\x74','\x72','\x75','\x65',
'\x20','\x5b','\x5d','\x20','\x5b','\x5d','\x20','\x5b',
'\x5d','\x20','\x5d','\x0a',
};

inline constexpr std::string_view fail35{std::begin(detail_fail35_bytes), std::end(detail_fail35_bytes)};
}
