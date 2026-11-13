// Screen language. English is written straight into the code; T() swaps in the translation
// when another language is chosen, and falls back to the English if there isn't one.
// Kept free of Arduino code so it can be tested on a PC.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gridcore {

enum Lang : uint8_t { LANG_EN = 0, LANG_FR };

void set_lang(Lang l);
Lang current_lang();
Lang lang_from_code(const char *code);   // "fr" -> LANG_FR, anything else -> LANG_EN

// The text to show for an English string (format strings included: the translation keeps the
// same % codes in the same order).
const char *T(const char *en);

// Numbers the way the language writes them: "20.4" / "20,4", "44,436" / "44 436"
void format_decimal(float v, int decimals, char *out, size_t cap);
void format_thousands(long n, char *out, size_t cap);

// Time of day for the header: "1:20 pm" in English, "13:20" in French
void format_header_clock(int minute_of_day, char *out, size_t cap);

}  // namespace gridcore
