#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <regex>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Abbreviation table
// ---------------------------------------------------------------------------

static const std::unordered_map<std::string, std::string> ABBREVIATIONS = {
    {"mrs",    "misess"},
    {"mr",     "mister"},
    {"dr",     "doctor"},
    {"st",     "saint"},
    {"co",     "company"},
    {"jr",     "junior"},
    {"maj",    "major"},
    {"gen",    "general"},
    {"drs",    "doctors"},
    {"rev",    "reverend"},
    {"lt",     "lieutenant"},
    {"hon",    "honorable"},
    {"sgt",    "sergeant"},
    {"capt",   "captain"},
    {"esq",    "esquire"},
    {"ltd",    "limited"},
    {"col",    "colonel"},
    {"ft",     "foot"},
    {"pty",    "proprietary"},
    {"vs",     "versus"},
    {"approx", "approximately"},
    {"dept",   "department"},
    {"prof",   "professor"},
    {"eg",     "for example"},
    {"ie",     "that is"},
    {"etc",    "et cetera"},
};

// ---------------------------------------------------------------------------
// Number-to-words helpers
// ---------------------------------------------------------------------------

static const char* ONES[] = {
    "", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
    "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen",
    "seventeen", "eighteen", "nineteen"
};

static const char* TENS[] = {
    "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"
};

static const char* SCALES[] = {
    "thousand", "million", "billion", "trillion", "quadrillion", "quintillion"
};

static const char* ORDINAL_ONES[] = {
    "", "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth",
    "tenth", "eleventh", "twelfth", "thirteenth", "fourteenth", "fifteenth", "sixteenth",
    "seventeenth", "eighteenth", "nineteenth"
};

static const char* ORDINAL_TENS[] = {
    "", "", "twentieth", "thirtieth", "fortieth", "fiftieth",
    "sixtieth", "seventieth", "eightieth", "ninetieth"
};

static std::string hundreds_to_words(int n) {
    if (n == 0) return "";
    std::string result;
    if (n >= 100) {
        result += ONES[n / 100];
        result += " hundred";
        if (n % 100 != 0) result += " and ";
    }
    int rem = n % 100;
    if (rem == 0) {
        // nothing
    } else if (rem < 20) {
        result += ONES[rem];
    } else {
        result += TENS[rem / 10];
        if (rem % 10 != 0) {
            result += " ";
            result += ONES[rem % 10];
        }
    }
    // trim trailing spaces
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

static std::string hundreds_to_ordinal(int n) {
    if (n == 0) return "";
    std::string result;
    if (n >= 100) {
        if (n % 100 == 0) {
            result += ONES[n / 100];
            result += " hundredth";
            return result;
        }
        result += ONES[n / 100];
        result += " hundred and ";
    }
    int rem = n % 100;
    if (rem < 20) {
        result += ORDINAL_ONES[rem];
    } else if (rem % 10 == 0) {
        result += ORDINAL_TENS[rem / 10];
    } else {
        result += TENS[rem / 10];
        result += " ";
        result += ORDINAL_ONES[rem % 10];
    }
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

static std::string long_to_words(long long n) {
    if (n == 0) return "zero";
    if (n < 0) return "minus " + long_to_words(-n);

    std::vector<int> groups;
    long long rem = n;
    while (rem > 0) {
        groups.insert(groups.begin(), (int)(rem % 1000));
        rem /= 1000;
    }

    std::string result;
    for (size_t i = 0; i < groups.size(); ++i) {
        int chunk = groups[i];
        if (chunk == 0) continue;
        std::string words = hundreds_to_words(chunk);
        int scale_idx = (int)groups.size() - (int)i - 2;
        if (scale_idx >= 0 && scale_idx < 6) {
            words += " ";
            words += SCALES[scale_idx];
        }
        if (!result.empty()) result += " ";
        result += words;
    }
    return result;
}

static std::string long_to_ordinal(long long n) {
    if (n == 0) return "zeroth";
    if (n < 0) return "minus " + long_to_ordinal(-n);

    std::vector<int> groups;
    long long rem = n;
    while (rem > 0) {
        groups.insert(groups.begin(), (int)(rem % 1000));
        rem /= 1000;
    }

    int non_zero = 0;
    for (int g : groups) if (g != 0) ++non_zero;

    std::string result;
    int last_non_zero = -1;
    for (int i = (int)groups.size() - 1; i >= 0; --i) {
        if (groups[i] != 0) { last_non_zero = i; break; }
    }

    for (size_t i = 0; i < groups.size(); ++i) {
        int chunk = groups[i];
        if (chunk == 0) continue;
        int scale_idx = (int)groups.size() - (int)i - 2;
        bool is_last = ((int)i == last_non_zero);
        std::string words = is_last ? hundreds_to_ordinal(chunk) : hundreds_to_words(chunk);
        if (scale_idx >= 0 && scale_idx < 6) {
            words += " ";
            words += SCALES[scale_idx];
        }
        if (!result.empty()) result += " ";
        result += words;
    }
    return result;
}

static std::string digit_to_word(char c) {
    switch (c) {
        case '0': return "zero";  case '1': return "one";
        case '2': return "two";   case '3': return "three";
        case '4': return "four";  case '5': return "five";
        case '6': return "six";   case '7': return "seven";
        case '8': return "eight"; case '9': return "nine";
        default:  return std::string(1, c);
    }
}

// ---------------------------------------------------------------------------
// normalize_text
// ---------------------------------------------------------------------------

std::string normalize_text(const std::string& text) {
    std::string result = text;

    // 1. Dotted multi-character abbreviations
    {
        std::regex eg_re(R"(\be\.g\.?)", std::regex::icase);
        result = std::regex_replace(result, eg_re, "for example");
        std::regex ie_re(R"(\bi\.e\.?)", std::regex::icase);
        result = std::regex_replace(result, ie_re, "that is");
        std::regex etc_re(R"(\betc\.?)", std::regex::icase);
        result = std::regex_replace(result, etc_re, "et cetera");
    }

    // 2. Ordinal numbers (must run before plain number pass)
    {
        std::regex ord_re(R"(\b(\d[\d,]*)(st|nd|rd|th)\b)", std::regex::icase);
        std::string out;
        std::sregex_iterator it(result.begin(), result.end(), ord_re);
        std::sregex_iterator end;
        size_t last_pos = 0;
        for (; it != end; ++it) {
            const std::smatch& m = *it;
            out += result.substr(last_pos, m.position() - last_pos);
            std::string raw = m[1].str();
            raw.erase(std::remove(raw.begin(), raw.end(), ','), raw.end());
            try {
                long long n = std::stoll(raw);
                out += long_to_ordinal(n);
            } catch (...) {
                out += m[0].str();
            }
            last_pos = m.position() + m.length();
        }
        out += result.substr(last_pos);
        result = out;
    }

    // 3. Numbers → words
    {
        std::regex num_re(R"(-?\d[\d,]*(?:\.\d+)?(?![a-zA-Z]))");
        std::string out;
        std::sregex_iterator it(result.begin(), result.end(), num_re);
        std::sregex_iterator end;
        size_t last_pos = 0;
        for (; it != end; ++it) {
            const std::smatch& m = *it;
            out += result.substr(last_pos, m.position() - last_pos);
            std::string raw = m[0].str();
            raw.erase(std::remove(raw.begin(), raw.end(), ','), raw.end());
            size_t dot = raw.find('.');
            try {
                if (dot != std::string::npos) {
                    bool neg = raw[0] == '-';
                    std::string int_part = raw.substr(neg ? 1 : 0, dot - (neg ? 1 : 0));
                    long long int_val = std::stoll(int_part.empty() ? "0" : int_part);
                    std::string int_words = neg ? "minus " + long_to_words(int_val) : long_to_words(int_val);
                    std::string dec_part = raw.substr(dot + 1);
                    std::string dec_words;
                    for (char c : dec_part) {
                        if (!dec_words.empty()) dec_words += " ";
                        dec_words += digit_to_word(c);
                    }
                    out += int_words + " point " + dec_words;
                } else {
                    long long n = std::stoll(raw);
                    out += long_to_words(n);
                }
            } catch (...) {
                out += m[0].str();
            }
            last_pos = m.position() + m.length();
        }
        out += result.substr(last_pos);
        result = out;
    }

    // 4. Word-level abbreviations
    {
        std::regex abbrev_re(R"(\b([A-Za-z]+)\.?(?=[\s,;:!?]|$))");
        std::string out;
        std::sregex_iterator it(result.begin(), result.end(), abbrev_re);
        std::sregex_iterator end;
        size_t last_pos = 0;
        for (; it != end; ++it) {
            const std::smatch& m = *it;
            out += result.substr(last_pos, m.position() - last_pos);
            std::string word = m[1].str();
            std::string lower = word;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            auto abbr_it = ABBREVIATIONS.find(lower);
            if (abbr_it != ABBREVIATIONS.end()) {
                out += abbr_it->second;
            } else {
                out += m[0].str();
            }
            last_pos = m.position() + m.length();
        }
        out += result.substr(last_pos);
        result = out;
    }

    return result;
}

// ---------------------------------------------------------------------------
// UTF-8 char splitter
// ---------------------------------------------------------------------------

std::vector<std::string> utf8_chars(const std::string& s) {
    std::vector<std::string> result;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        size_t char_len;
        if      (c < 0x80) char_len = 1;
        else if (c < 0xE0) char_len = 2;
        else if (c < 0xF0) char_len = 3;
        else               char_len = 4;
        result.push_back(s.substr(i, char_len));
        i += char_len;
    }
    return result;
}
