#ifndef ENCODING_CONFIG_H
#define ENCODING_CONFIG_H

#include <nlohmann/json.hpp>

// Loads encoding_config.json next to the executable if present.
// Returns empty object when file does not exist or fails to parse.
[[nodiscard]] nlohmann::json loadEncodingConfig();

#endif // ENCODING_CONFIG_H
