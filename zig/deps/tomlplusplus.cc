// Compile the TOML parser once instead of in every settings consumer.
#define TOML_IMPLEMENTATION
#include <toml++/toml.h>
