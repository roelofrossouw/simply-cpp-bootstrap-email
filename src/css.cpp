// A port of the css_parser gem (1.22, MIT, https://github.com/premailer/css_parser): the parts premailer uses to put
// a stylesheet into style attributes. Its regular expressions are kept as they are (in ECMAScript form), so values
// come apart and together the way they do in Ruby.

#include "css.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>

namespace sc::bootstrap_email::css {
    namespace {
        using std::regex;
        constexpr auto ci = regex::ECMAScript | regex::icase;

        std::string strip(std::string_view s) {
            const auto first = s.find_first_not_of(" \t\n\r\f\v");
            if (first == std::string_view::npos) {
                return {};
            }
            return std::string(s.substr(first, s.find_last_not_of(" \t\n\r\f\v") - first + 1));
        }

        std::string lower(std::string_view s) {
            std::string out(s);
            std::ranges::transform(out, out.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        std::string normalize(std::string_view property) { return lower(strip(property)); }

        // Ruby's String#slice!(regex): takes the first match out of s, and returns it
        std::optional<std::string> slice(std::string &s, const regex &re) {
            std::smatch match;
            if (!std::regex_search(s, match, re)) {
                return std::nullopt;
            }
            std::string found = match.str(0);
            s.erase(static_cast<std::size_t>(match.position(0)), static_cast<std::size_t>(match.length(0)));
            return found;
        }

        // css_parser's regexps.rb
        const std::string colour_names = "aliceblue|antiquewhite|aqua|aquamarine|azure|beige|bisque|black|blanchedalmond|blue|blueviolet|brown|burlywood|cadetblue|chartreuse|chocolate|coral|cornflowerblue|cornsilk|crimson|cyan|darkblue|darkcyan|darkgoldenrod|darkgray|darkgreen|darkgrey|darkkhaki|darkmagenta|darkolivegreen|darkorange|darkorchid|darkred|darksalmon|darkseagreen|darkslateblue|darkslategray|darkslategrey|darkturquoise|darkviolet|deeppink|deepskyblue|dimgray|dimgrey|dodgerblue|firebrick|floralwhite|forestgreen|fuchsia|gainsboro|ghostwhite|gold|goldenrod|gray|green|greenyellow|grey|honeydew|hotpink|indianred|indigo|ivory|khaki|lavender|lavenderblush|lawngreen|lemonchiffon|lightblue|lightcoral|lightcyan|lightgoldenrodyellow|lightgray|lightgreen|lightgrey|lightpink|lightsalmon|lightseagreen|lightskyblue|lightslategray|lightslategrey|lightsteelblue|lightyellow|lime|limegreen|linen|magenta|maroon|mediumaquamarine|mediumblue|mediumorchid|mediumpurple|mediumseagreen|mediumslateblue|mediumspringgreen|mediumturquoise|mediumvioletred|midnightblue|mintcream|mistyrose|moccasin|navajowhite|navy|oldlace|olive|olivedrab|orange|orangered|orchid|palegoldenrod|palegreen|paleturquoise|palevioletred|papayawhip|peachpuff|peru|pink|plum|powderblue|purple|red|rosybrown|royalblue|saddlebrown|salmon|sandybrown|seagreen|seashell|sienna|silver|skyblue|slateblue|slategray|slategrey|snow|springgreen|steelblue|tan|teal|thistle|tomato|turquoise|violet|wheat|white|whitesmoke|yellow|yellowgreen|transparent|inherit|currentColor";
        const std::string length_or_percentage = R"(([\-]*(([0-9]*\.[0-9]+)|[0-9]+)(e[mx]+|px|[cm]+m|p[tc+]|in|%)))";
        const std::string background_position = "(?:" + length_or_percentage + "|left|center|right|top|bottom)";
        const std::string background_size = "(?:" + length_or_percentage + "|auto|cover|contain|initial|inherit)";

        std::string possible_values(const std::string &values) { return "([ ]*^)?(" + values + ")([ ]*$)?"; }

        const regex &important_rx() {
            static const regex re(R"(\s*!important\b\s*)", ci);
            return re;
        }
        const regex &colour_rx() {
            static const regex re(
                R"(\b(hsl|rgb)\s*\(-?\s*-?\d+(\.\d+)?%?\s*%?,-?\s*-?\d+(\.\d+)?%?\s*%?,-?\s*-?\d+(\.\d+)?%?\s*%?\))"
                R"(|\b(hsla|rgba)\s*\(-?\s*-?\d+(\.\d+)?%?\s*%?,-?\s*-?\d+(\.\d+)?%?\s*%?,-?\s*-?\d+(\.\d+)?%?\s*%?,-?\s*-?\d+(\.\d+)?%?\s*%?\))"
                R"(|\s*#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b)"
                R"(|\s*\b()" + colour_names + R"()\b)", ci);
            return re;
        }
        const regex &border_units_rx() {
            static const regex re(
                R"((auto|inherit|0|(-*([0-9]+|[0-9]*\.[0-9]+)(rem|vw|vh|vm|vmin|vmax|e[mx]+|px|[cm]+m|p[tc+]|in|%)))([\s;]|$))"
                R"(|(thin|medium|thick))", ci);
            return re;
        }
        const regex &border_style_rx() {
            static const regex re(
                R"((\s*^)?(none|hidden|dotted|dashed|solid|double|dot-dash|dot-dot-dash|wave|groove|ridge|inset|outset)(\s*$)?)", ci);
            return re;
        }
        const regex &inherit_rx() {
            static const regex re(possible_values("inherit"), ci);
            return re;
        }
        const regex &uri_rx() {
            static const regex re(R"x(url\(("([^"]*)"|'([^']*)'|([^)]*))\))x", ci);
            return re;
        }
        const regex &image_rx() {
            static const regex re(R"x(url\(("([^"]*)"|'([^']*)'|([^)]*))\)|[-a-z]*gradient\([-a-z0-9 .,#%()]*\)|none)x", ci);
            return re;
        }
        const regex &uri_or_none_rx() {
            static const regex re(R"x(url\(("([^"]*)"|'([^']*)'|([^)]*))\)|none)x", ci);
            return re;
        }
        const regex &scroll_fixed_rx() {
            static const regex re(possible_values("scroll|fixed"), ci);
            return re;
        }
        const regex &repeat_rx() {
            static const regex re(possible_values("repeat(-x|-y)*|no-repeat"), ci);
            return re;
        }
        const regex &inside_outside_rx() {
            static const regex re(possible_values("inside|outside"), ci);
            return re;
        }
        const regex &list_style_type_rx() {
            static const regex re(possible_values(
                "disc|circle|square|decimal-leading-zero|decimal|lower-roman|upper-roman|lower-greek|lower-alpha|lower-latin|"
                "upper-alpha|upper-latin|hebrew|armenian|georgian|cjk-ideographic|hiragana|hira-gana-iroha|katakana-iroha|katakana|none"), ci);
            return re;
        }
        const regex &background_position_rx() {
            static const regex re(background_position + R"(\s+)" + background_position + "|" + background_position, ci);
            return re;
        }
        const regex &background_size_rx() {
            static const regex re(R"(\s*/\s*()" + background_size + R"(\s+)" + background_size + "|" + background_size + ")", ci);
            return re;
        }
        const regex &font_units_rx() {
            static const regex re(R"(((x+-)*small|medium|larger*|auto|inherit|([0-9]+|[0-9]*\.[0-9]+)(e[mx]+|px|[cm]+m|p[tc+]|in|%)*))", ci);
            return re;
        }

        const std::vector<std::string> background_properties = {
            "background-color", "background-image", "background-repeat", "background-position", "background-size",
            "background-attachment"};
        const std::vector<std::string> list_style_properties = {"list-style-type", "list-style-position", "list-style-image"};
        const std::vector<std::string> font_style_properties = {
            "font-style", "font-variant", "font-weight", "font-size", "line-height", "font-family"};
        const std::vector<std::string> border_style_properties = {"border-width", "border-style", "border-color"};
        const std::vector<std::string> border_properties = {"border", "border-left", "border-right", "border-top", "border-bottom"};
        const std::vector<std::pair<std::string, std::vector<std::string>>> dimensions = {
            {"margin", {"margin-top", "margin-right", "margin-bottom", "margin-left"}},
            {"padding", {"padding-top", "padding-right", "padding-bottom", "padding-left"}},
            {"border-color", {"border-top-color", "border-right-color", "border-bottom-color", "border-left-color"}},
            {"border-style", {"border-top-style", "border-right-style", "border-bottom-style", "border-left-style"}},
            {"border-width", {"border-top-width", "border-right-width", "border-bottom-width", "border-left-width"}},
        };

        // Splits a value at its spaces, keeping a function's arguments together: "1px calc(2px + 3px)" is two
        std::vector<std::string> split_value(std::string_view value) {
            constexpr std::string_view placeholder = "___SPACE___";
            std::string kept;
            for (std::size_t i = 0; i < value.size();) {
                // A function: a name and its balanced parentheses
                std::size_t name_end = i;
                while (name_end < value.size() && (std::isalnum(static_cast<unsigned char>(value[name_end])) || value[name_end] == '-')) {
                    ++name_end;
                }
                if (name_end > i && name_end < value.size() && value[name_end] == '(') {
                    int depth = 0;
                    std::size_t end = name_end;
                    for (; end < value.size(); ++end) {
                        depth += value[end] == '(' ? 1 : value[end] == ')' ? -1 : 0;
                        if (depth == 0) {
                            break;
                        }
                    }
                    if (end < value.size()) {
                        for (std::size_t j = i; j <= end;) {
                            if (std::isspace(static_cast<unsigned char>(value[j]))) {
                                while (j <= end && std::isspace(static_cast<unsigned char>(value[j]))) {
                                    ++j;
                                }
                                kept += placeholder;
                            } else {
                                kept += value[j++];
                            }
                        }
                        i = end + 1;
                        continue;
                    }
                }
                if (name_end > i) {
                    kept.append(value.substr(i, name_end - i));
                    i = name_end;
                } else {
                    kept += value[i++];
                }
            }
            std::vector<std::string> parts;
            const std::string stripped = strip(kept);
            std::size_t start = 0;
            while (start < stripped.size()) {
                std::size_t end = start;
                while (end < stripped.size() && !std::isspace(static_cast<unsigned char>(stripped[end]))) {
                    ++end;
                }
                std::string part = stripped.substr(start, end - start);
                for (std::size_t at; (at = part.find(placeholder)) != std::string::npos;) {
                    part.replace(at, placeholder.size(), " ");
                }
                parts.push_back(part);
                while (end < stripped.size() && std::isspace(static_cast<unsigned char>(stripped[end]))) {
                    ++end;
                }
                start = end;
            }
            return parts;
        }

        bool unmatched_open_parenthesis(std::string_view s) {
            const auto open = s.find('(');
            return open != std::string_view::npos && s.find(')', open) == std::string_view::npos;
        }
    }

    std::optional<value> value::parse(std::string_view raw) {
        // \s*;\s*$ off the end
        std::string text(raw);
        if (const auto last = text.find_last_not_of(" \t\n\r\f\v"); last != std::string::npos && text[last] == ';') {
            const auto before = text.find_last_not_of(" \t\n\r\f\v", last == 0 ? std::string::npos : last - 1);
            text.erase(last == 0 || before == std::string::npos ? 0 : before + 1);
        }
        // The first \s*!important\b\s*, in any case
        bool important = false;
        for (std::size_t at = text.find('!'); at != std::string::npos; at = text.find('!', at + 1)) {
            if (lower(std::string_view(text).substr(at, 10)) == "!important" &&
                (at + 10 == text.size() || !(std::isalnum(static_cast<unsigned char>(text[at + 10])) || text[at + 10] == '_'))) {
                std::size_t start = at, end = at + 10;
                while (start > 0 && std::isspace(static_cast<unsigned char>(text[start - 1]))) {
                    --start;
                }
                while (end < text.size() && std::isspace(static_cast<unsigned char>(text[end]))) {
                    ++end;
                }
                text.erase(start, end - start);
                important = true;
                break;
            }
        }
        text = strip(text);
        if (text.empty()) {
            return std::nullopt;
        }
        return value{text, important};
    }

    const value *declarations::get(std::string_view property) const {
        const std::string name = normalize(property);
        const auto found = std::ranges::find(items, name, &std::pair<std::string, value>::first);
        return found == items.end() ? nullptr : &found->second;
    }

    value *declarations::get(std::string_view property) {
        return const_cast<value *>(std::as_const(*this).get(property));
    }

    void declarations::erase(std::string_view property) {
        const std::string name = normalize(property);
        std::erase_if(items, [&](const auto &item) { return item.first == name; });
    }

    void declarations::set(std::string_view property, const value &v) {
        const std::string name = normalize(property);
        if (value *current = get(name)) {
            if (!current->important || v.important) {
                *current = v;
            }
            return;
        }
        items.emplace_back(name, v);
    }

    void declarations::set(std::string_view property, std::string_view raw) {
        if (strip(raw).empty()) {
            erase(property);
            return;
        }
        if (const auto v = value::parse(raw)) {
            set(property, *v);
        }
    }

    void declarations::replace(std::string_view property, const std::vector<std::pair<std::string, std::optional<std::string>>> &replacements,
                               const bool preserve_importance) {
        const std::string name = normalize(property);
        const value *shorthand = get(name);
        if (shorthand == nullptr) {
            return;
        }
        declarations parts;
        for (const auto &[key, text] : replacements) {
            if (text) {
                parts.set(key, *text);
            }
        }
        if (preserve_importance) {
            for (auto &[key, part] : parts.items) {
                part.important = shorthand->important;
            }
        }
        auto list = items;
        auto index_of = [&](const std::string &key) {
            return static_cast<std::size_t>(std::ranges::find(list, key, &std::pair<std::string, value>::first) - list.begin());
        };
        std::size_t index = index_of(name);
        std::vector<std::pair<std::string, value>> result;
        for (const auto &[key, part] : parts.items) {
            const value *existing = get(key);
            if (existing == nullptr) {
                result.emplace_back(key, part);
                continue;
            }
            if (part.important && !existing->important) {
                result.emplace_back(key, part);
                const std::size_t replaced = index_of(key);
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(replaced));
                if (replaced < index) {
                    --index;
                }
                continue;
            }
            if (!part.important && existing->important) {
                continue;
            }
            if (index > index_of(key)) {
                result.emplace_back(key, part);
            }
        }
        if (result.empty()) {
            return;
        }
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
        list.insert(list.begin() + static_cast<std::ptrdiff_t>(index), result.begin(), result.end());
        // A key twice keeps its first place and its last value, as Ruby's to_h does
        items.clear();
        for (auto &[key, part] : list) {
            if (value *seen = get(key)) {
                *seen = part;
            } else {
                items.emplace_back(key, part);
            }
        }
    }

    std::string declarations::to_string() const {
        std::string out;
        for (const auto &[property, v] : items) {
            out += property + ": " + v.text + (v.important ? " !important" : "") + "; ";
        }
        std::erase_if(out, [](const char c) { return c == '\n' || c == '\r' || c == '\f' || c == '\t'; });
        const auto first = out.find_first_not_of(" \v^({)");
        out = first == std::string::npos ? std::string() : out.substr(first);
        return strip(out);
    }

    void declarations::expand_shorthands() {
        expand_border();
        expand_dimensions();
        expand_font();
        expand_background();
        expand_list_style();
    }

    void declarations::expand_border() {
        for (const auto &property : border_properties) {
            const value *v = get(property);
            if (v == nullptr) {
                continue;
            }
            std::string text = v->text;
            const auto width = slice(text, border_units_rx());
            const auto colour = slice(text, colour_rx());
            const auto style = slice(text, border_style_rx());
            replace(property, {{property + "-width", width}, {property + "-color", colour}, {property + "-style", style}}, true);
        }
    }

    void declarations::expand_dimensions() {
        static const regex comma(R"(\s*,\s*)");
        for (const auto &[property, sides] : dimensions) {
            const value *v = get(property);
            if (v == nullptr) {
                continue;
            }
            // Colours' commas without their spaces, so "rgb(1, 2, 3)" stays one value
            std::string text;
            std::string rest = v->text;
            std::smatch match;
            while (std::regex_search(rest, match, colour_rx())) {
                text += match.prefix().str() + std::regex_replace(match.str(0), comma, ",");
                rest = match.suffix().str();
            }
            text += rest;
            std::vector<std::string> values = split_value(text);
            switch (values.size()) {
                case 1: values = {values[0], values[0], values[0], values[0]}; break;
                case 2: values = {values[0], values[1], values[0], values[1]}; break;
                case 3: values.push_back(values[1]); break;
                case 4: break;
                default: continue; // css_parser gives up on these too ("Cannot parse")
            }
            replace(property, {{sides[0], values[0]}, {sides[1], values[1]}, {sides[2], values[2]}, {sides[3], values[3]}}, true);
        }
    }

    void declarations::expand_font() {
        const value *v = get("font");
        if (v == nullptr) {
            return;
        }
        std::vector<std::pair<std::string, std::optional<std::string>>> font = {
            {"font-style", "normal"}, {"font-variant", "normal"}, {"font-weight", "normal"}, {"font-size", "normal"},
            {"line-height", "normal"}};
        auto at = [&](const std::string &key) -> std::optional<std::string> & {
            for (auto &[name, text] : font) {
                if (name == key) {
                    return text;
                }
            }
            return font.emplace_back(key, std::nullopt).second;
        };
        static const regex slash_space(R"(/\s+)");
        static const regex parts(R"x("(?:.*[^"])"|'(?:.*[^'])'|(?:\w[^ ,]+))x");
        static const regex style(R"(italic|oblique)", ci);
        static const regex variant(R"(small-caps)", ci);
        static const regex weight(R"([1-9]00$|bold|bolder|lighter)", ci);
        static const regex trailing_semicolon(R"(;$)");
        const std::string text = std::regex_replace(v->text, slash_space, "/");
        bool in_fonts = false;
        for (std::sregex_iterator it(text.begin(), text.end(), parts), end; it != end; ++it) {
            std::string part = std::regex_replace(strip(it->str()), trailing_semicolon, "");
            if (in_fonts) {
                auto &family = at("font-family");
                family = family ? *family + ", " + part : part;
            } else if (static const regex normal("normal|inherit", ci); std::regex_search(part, normal)) {
                // css_parser sets font-style, -weight and -variant to it when they aren't set yet, and they always are
            } else if (std::regex_search(part, style)) {
                at("font-style") = part;
            } else if (std::regex_search(part, variant)) {
                at("font-variant") = part;
            } else if (std::regex_search(part, weight)) {
                at("font-weight") = part;
            } else if (std::regex_search(part, font_units_rx())) {
                if (const auto slash = part.find('/'); slash != std::string::npos) {
                    at("font-size") = part.substr(0, slash);
                    at("line-height") = part.substr(slash + 1);
                } else {
                    at("font-size") = part;
                }
                in_fonts = true;
            }
        }
        replace("font", font, true);
    }

    void declarations::expand_background() {
        const value *v = get("background");
        if (v == nullptr) {
            return;
        }
        std::string text = v->text;
        if (std::regex_search(text, inherit_rx())) {
            std::vector<std::pair<std::string, std::optional<std::string>>> inherit;
            for (const auto &property : background_properties) {
                inherit.emplace_back(property, "inherit");
            }
            replace("background", inherit, true);
            return;
        }
        const auto image = slice(text, image_rx());
        const auto attachment = slice(text, scroll_fixed_rx());
        const auto repeat = slice(text, repeat_rx());
        const auto colour = slice(text, colour_rx());
        auto size = slice(text, background_size_rx());
        if (size) {
            static const regex leading_slash(R"(^\s*/\s*)");
            size = std::regex_replace(*size, leading_slash, "");
        }
        const auto position = slice(text, background_position_rx());
        replace("background", {{"background-image", image}, {"background-attachment", attachment}, {"background-repeat", repeat},
                               {"background-color", colour}, {"background-size", size}, {"background-position", position}}, true);
    }

    void declarations::expand_list_style() {
        const value *v = get("list-style");
        if (v == nullptr) {
            return;
        }
        std::string text = v->text;
        if (std::regex_search(text, inherit_rx())) {
            replace("list-style", {{list_style_properties[0], "inherit"}, {list_style_properties[1], "inherit"},
                                   {list_style_properties[2], "inherit"}}, true);
            return;
        }
        const auto type = slice(text, list_style_type_rx());
        const auto position = slice(text, inside_outside_rx());
        const auto image = slice(text, uri_or_none_rx());
        replace("list-style", {{"list-style-type", type}, {"list-style-position", position}, {"list-style-image", image}}, true);
    }

    void declarations::create_shorthands() {
        create_background();
        create_dimensions();
        create_border();
        create_font();
        create_from(list_style_properties, "list-style");
    }

    void declarations::create_from(const std::vector<std::string> &properties, std::string_view shorthand) {
        std::vector<std::string> values;
        std::vector<std::string> taken;
        for (const auto &property : properties) {
            const value *v = get(property);
            if (v == nullptr || v->important) {
                continue;
            }
            values.push_back(v->text);
            taken.push_back(property);
        }
        if (values.size() <= 1) {
            return;
        }
        for (const auto &property : taken) {
            erase(property);
        }
        std::string joined;
        for (const auto &v : values) {
            joined += (joined.empty() ? "" : " ") + v;
        }
        set(shorthand, joined);
    }

    void declarations::create_background() {
        if (const value *size = get("background-size"); size != nullptr && !size->important) {
            if (!has("background-position")) {
                set("background-position", "0% 0%");
            }
            value *again = get("background-size");
            if (const auto changed = value::parse("/ " + again->text)) {
                *again = *changed;
            }
        }
        create_from(background_properties, "background");
    }

    void declarations::create_border() {
        static const regex comma_space(R"(,\s)");
        std::vector<std::string> values;
        for (const auto &property : border_style_properties) {
            const value *v = get(property);
            if (v == nullptr || v->important) {
                continue;
            }
            const std::string compact = strip(std::regex_replace(v->text, comma_space, ","));
            if (std::ranges::any_of(compact, [](const unsigned char c) { return std::isspace(c); })) {
                continue;
            }
            values.push_back(v->text);
        }
        if (values.size() != border_style_properties.size()) {
            return;
        }
        for (const auto &property : border_style_properties) {
            erase(property);
        }
        set("border", values[0] + " " + values[1] + " " + values[2]);
    }

    void declarations::create_dimensions() {
        if (size() < 4) {
            return;
        }
        for (const auto &[property, sides] : dimensions) {
            std::vector<std::string> values;
            for (const auto &side : sides) {
                if (const value *v = get(side)) {
                    values.push_back(v->text);
                }
            }
            if (values.size() != 4) {
                continue;
            }
            const std::string &top = values[0], &right = values[1], &bottom = values[2], &left = values[3];
            std::vector<std::string> picked;
            if (top == right && top == bottom && top == left) {
                picked = {top};
            } else if (left != right) {
                picked = {top, right, bottom, left};
            } else if (top == bottom) {
                picked = {top, left};
            } else {
                picked = {top, left, bottom};
            }
            std::string joined;
            for (const auto &v : picked) {
                joined += (joined.empty() ? "" : " ") + v;
            }
            joined = strip(joined);
            if (!joined.empty()) {
                set(property, joined);
            }
            for (const auto &side : sides) {
                erase(side);
            }
        }
    }

    void declarations::create_font() {
        if (!std::ranges::all_of(font_style_properties, [&](const std::string &property) { return has(property); })) {
            return;
        }
        std::string font;
        for (const char *property : {"font-style", "font-variant", "font-weight"}) {
            if (get(property)->text != "normal") {
                font += get(property)->text + " ";
            }
        }
        font += get("font-size")->text;
        if (get("line-height")->text != "normal") {
            font += "/" + get("line-height")->text;
        }
        font += " " + get("font-family")->text;
        static const regex spaces(R"(\s+)");
        set("font", std::regex_replace(font, spaces, " "));
        for (const auto &property : font_style_properties) {
            erase(property);
        }
    }

    declarations parse_declarations(std::string_view block) {
        declarations parsed;
        std::optional<std::string> continuation;
        std::vector<std::string> pieces;
        for (std::size_t start = 0; start <= block.size();) {
            const auto end = std::min(block.find(';', start), block.size());
            pieces.emplace_back(block.substr(start, end - start));
            start = end + 1;
        }
        // Ruby's split drops trailing empty pieces
        while (!pieces.empty() && pieces.back().empty()) {
            pieces.pop_back();
        }
        for (auto piece : pieces) {
            if (continuation) {
                piece = *continuation + ";" + piece;
            }
            if (unmatched_open_parenthesis(piece)) {
                continuation = piece;
                continue;
            }
            const auto colon = piece.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            const std::string property = strip(std::string_view(piece).substr(0, colon));
            const std::string text = strip(std::string_view(piece).substr(colon + 1));
            if (property.empty() || text.empty() || lower(text) == "!important") {
                continue;
            }
            parsed.set(property, text);
            continuation.reset();
        }
        return parsed;
    }

    int specificity(std::string_view selector) {
        static const regex non_id(
            R"((?:\.\w+)|\[(?:\w+)|(?::(?:link|visited|active|hover|focus|lang|target|enabled|disabled|checked|indeterminate|root)"
            R"(|nth-child|nth-last-child|nth-of-type|nth-last-of-type|first-child|last-child|first-of-type|last-of-type|only-child)"
            R"(|only-of-type|empty|contains)))", ci);
        static const regex elements(R"((?:(?:^|[\s+>~]+)\w+|:{1,2}(?:after|before|first-letter|first-line|selection)))", ci);
        const std::string s(selector);
        const auto ids = std::ranges::count(s, '#');
        const auto others = std::distance(std::sregex_iterator(s.begin(), s.end(), non_id), std::sregex_iterator());
        const auto names = std::distance(std::sregex_iterator(s.begin(), s.end(), elements), std::sregex_iterator());
        return std::stoi("0" + std::to_string(ids) + std::to_string(others) + std::to_string(names));
    }

    declarations merge(std::vector<std::pair<int, declarations>> rule_sets) {
        if (rule_sets.size() == 1) {
            return rule_sets[0].second;
        }
        struct folded {
            std::string property;
            std::string text;
            int specificity;
            bool important;
        };
        std::vector<folded> properties;
        for (auto &[specificity, rule_set] : rule_sets) {
            rule_set.expand_shorthands();
            for (const auto &[property, v] : rule_set.list()) {
                auto found = std::ranges::find(properties, property, &folded::property);
                if (found == properties.end()) {
                    properties.push_back({property, v.text, specificity, v.important});
                } else if (v.important) {
                    if (!found->important || found->specificity <= specificity) {
                        *found = {property, v.text, specificity, v.important};
                    }
                } else if (found->specificity <= specificity && !found->important) {
                    *found = {property, v.text, specificity, v.important};
                }
            }
        }
        declarations merged;
        for (const auto &[property, text, specificity, important] : properties) {
            std::string v = strip(text);
            if (important && !v.empty() && v.back() == ';') {
                v.pop_back();
            }
            merged.set(strip(property), important ? v + "!important" : v);
        }
        merged.create_shorthands();
        return merged;
    }

    std::vector<rule> parse_stylesheet(std::string_view source) {
        std::string css(source);
        for (std::size_t at; (at = css.find("/*")) != std::string::npos;) {
            const auto end = css.find("*/", at + 2);
            css.erase(at, end == std::string::npos ? std::string::npos : end + 2 - at);
        }
        for (const std::string_view marker : {"<!--", "-->"}) {
            for (std::size_t at; (at = css.find(marker)) != std::string::npos;) {
                css.erase(at, marker.size());
            }
        }
        css = std::regex_replace(css, regex(R"x(@import\s+(url\()?["']?(.[^'"\s]*)["']?\)?([\w\s,^\])]*)\)?;?)x"), "");

        std::vector<rule> rules;
        auto add = [&](std::string selectors, const std::string &block, const std::vector<std::string> &media) {
            rule r;
            for (std::size_t start = 0; start <= selectors.size();) {
                const auto end = std::min(selectors.find(',', start), selectors.size());
                static const regex spaces(R"(\s+)");
                r.selectors.push_back(strip(std::regex_replace(selectors.substr(start, end - start), spaces, " ")));
                start = end + 1;
            }
            while (!r.selectors.empty() && r.selectors.back().empty()) {
                r.selectors.pop_back();
            }
            r.block = parse_declarations(block);
            r.media = media;
            rules.push_back(std::move(r));
        };
        auto sanitize = [](const std::string &query) {
            static const regex spaces(R"(\s+)");
            const std::string collapsed = strip(std::regex_replace(query, spaces, " "));
            return collapsed.empty() ? std::string("all") : collapsed;
        };

        // css_parser's tokens: whitespace, backslashes, braces and quotes, parentheses, and runs of anything else
        std::vector<std::string> tokens;
        for (std::size_t i = 0; i < css.size();) {
            const auto is_space = [](const char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
            std::size_t end = i + 1;
            if (is_space(css[i])) {
                while (end < css.size() && is_space(css[end])) {
                    ++end;
                }
            } else if (css[i] == '\\' && i + 1 < css.size() && css[i + 1] == '\\') {
                while (end < css.size() && css[end] == '\\') {
                    ++end;
                }
            } else if (css[i] == '\\' && i + 1 < css.size() && (css[i + 1] == '{' || css[i + 1] == '}' || css[i + 1] == '"' || is_space(css[i + 1]))) {
                end = i + 2;
            } else if (css[i] == '{' || css[i] == '}' || css[i] == '"' || css[i] == '(' || css[i] == ')') {
                // one character
            } else {
                while (end < css.size() && !is_space(css[end]) && css[end] != '"' && css[end] != '{' && css[end] != '}' &&
                       css[end] != '(' && css[end] != ')' && css[end] != '\\') {
                    ++end;
                }
            }
            tokens.push_back(css.substr(i, end - i));
            i = end;
        }

        std::vector<std::string> media = {"all"};
        int in_declarations = 0;
        bool in_charset = false, in_string = false, in_at_media = false, in_media_block = false;
        std::string selectors, query, block;
        for (std::string token : tokens) {
            if (token.starts_with('"')) {
                in_string = !in_string;
            }
            if (in_declarations > 0) {
                if (in_declarations > 1) {
                    if (token.find('}') != std::string::npos) {
                        --in_declarations;
                    }
                    continue;
                }
                if (!in_string && token.find('{') != std::string::npos) {
                    ++in_declarations;
                    continue;
                }
                block += token;
                if (!in_string && token.find('}') != std::string::npos) {
                    if (const auto brace = block.find_last_of('}'); brace != std::string::npos &&
                        block.find_first_not_of(" \t\n\r\f\v", brace + 1) == std::string::npos) {
                        block.erase(brace);
                    }
                    --in_declarations;
                    block = strip(block);
                    if (!block.empty()) {
                        add(selectors, block, media);
                    }
                    selectors.clear();
                    block.clear();
                }
            } else if (lower(token).find("@media") != std::string::npos) {
                in_at_media = true;
                media.clear();
            } else if (in_at_media) {
                if (token.find('{') != std::string::npos) {
                    in_at_media = false;
                    in_media_block = true;
                    media.push_back(sanitize(query));
                    query.clear();
                } else if (token.find(',') != std::string::npos) {
                    std::ranges::replace(token, ',', ' ');
                    query += strip(token) + " ";
                    media.push_back(sanitize(query));
                    query.clear();
                } else {
                    token = strip(token);
                    if (token == "(") {
                        query += token;
                    } else if (token == ")") {
                        if (!query.empty() && query.back() == ' ') {
                            query.pop_back();
                        }
                        query += token;
                    } else {
                        query += token + " ";
                    }
                }
            } else if (in_charset || lower(token).find("@charset") != std::string::npos) {
                in_charset = token.find(';') == std::string::npos;
            } else if (!in_string && token.find('}') != std::string::npos) {
                if (in_media_block) {
                    media = {"all"};
                    in_media_block = false;
                }
            } else if (!in_string && token.find('{') != std::string::npos) {
                selectors = strip(selectors);
                ++in_declarations;
            } else {
                selectors += token;
            }
        }
        if (in_declarations > 0) {
            add(selectors, block, media);
        }
        return rules;
    }

    std::string premailer_selector(std::string_view selector) {
        static const regex link(R"(:link\s*)", ci);
        std::string s = std::regex_replace(std::string(selector), link, "");
        // Element names in lower case: a word at the start or after a space
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (i == 0 || std::isspace(static_cast<unsigned char>(s[i - 1]))) {
                for (std::size_t j = i; j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_'); ++j) {
                    s[j] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[j])));
                }
            }
        }
        return s;
    }

    bool unmergeable(std::string_view selector, const std::vector<std::string> &media) {
        static const regex pseudo(R"((:(visited|active|hover|focus|after|before|selection|target|first-(line|letter))|^@))", ci);
        const bool media_query = std::ranges::any_of(media, [](const std::string &query) {
            return std::ranges::count(query, '(') + std::ranges::count(query, ')') >= 2;
        });
        return media_query || std::regex_search(std::string(selector), pseudo);
    }

    std::string to_stylesheet(const std::vector<rule> &rules) {
        std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> by_media;
        for (const auto &r : rules) {
            const std::string block = r.block.to_string();
            for (const auto &query : r.media) {
                auto found = std::ranges::find(by_media, query, &decltype(by_media)::value_type::first);
                if (found == by_media.end()) {
                    by_media.emplace_back(query, std::vector<std::pair<std::string, std::string>>{});
                    found = by_media.end() - 1;
                }
                for (const auto &selector : r.selectors) {
                    found->second.emplace_back(selector, block);
                }
            }
        }
        std::vector<std::string> out;
        for (const auto &[query, styles] : by_media) {
            const bool media_block = query != "all";
            if (media_block) {
                out.push_back("@media " + query + " {");
            }
            for (const auto &[selector, block] : styles) {
                out.push_back(media_block ? "  " + selector + " {\n    " + block + "\n  }" : selector + " {\n" + block + "\n}");
            }
            if (media_block) {
                out.push_back("}");
            }
        }
        out.emplace_back();
        std::string joined;
        for (std::size_t i = 0; i < out.size(); ++i) {
            joined += (i ? "\n" : "") + out[i];
        }
        return joined;
    }
}
