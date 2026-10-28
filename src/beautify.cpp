#include "beautify.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <vector>

// htmlbeautifier's HtmlParser matches its patterns in order at each position. They're written out by hand here:
// std::regex recurses per character, and a page's <style> is one line of tens of kilobytes.

namespace sc::bootstrap_email {
    namespace {
        bool starts_with_ci(std::string_view text, std::size_t at, std::string_view prefix) {
            if (at + prefix.size() > text.size()) {
                return false;
            }
            for (std::size_t i = 0; i < prefix.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(text[at + i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
                    return false;
                }
            }
            return true;
        }

        std::size_t find_ci(std::string_view text, std::string_view what, std::size_t from) {
            for (std::size_t at = from; at + what.size() <= text.size(); ++at) {
                if (starts_with_ci(text, at, what)) {
                    return at;
                }
            }
            return std::string_view::npos;
        }

        // ELEMENT_CONTENT, (?:<%.*?%>|[^>])*, from at: where it stops (at a '>' or the end)
        std::size_t element_content_end(std::string_view text, std::size_t at) {
            while (at < text.size() && text[at] != '>') {
                if (text.compare(at, 2, "<%") == 0) {
                    const auto close = text.find("%>", at + 2);
                    if (close != std::string_view::npos) {
                        at = close + 2;
                        continue;
                    }
                }
                ++at;
            }
            return at;
        }

        const std::vector<std::string_view> void_elements = {
            "area", "base", "br", "col", "command", "embed", "hr", "img", "input", "keygen", "link", "meta", "param", "source",
            "track", "wbr"};
        const std::vector<std::string_view> block_elements = {
            "address", "article", "aside", "audio", "blockquote", "canvas", "dd", "details", "dir", "div", "dl", "dt",
            "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "li",
            "menu", "noframes", "noscript", "ol", "p", "pre", "section", "table", "tbody", "td", "tfoot", "th", "thead", "tr",
            "ul", "video"};

        enum class kind {
            embed, open_ie_cc, close_ie_cc, standalone, foreign_block, preformatted_block, close_block, open_block, close_element,
            open_element, new_lines, text
        };

        struct token {
            kind type;
            std::vector<std::string_view> parts;
            std::size_t length;
        };

        // <(names)(?: content)?(/)?>  — the names tried in order, each giving up when what follows doesn't fit
        std::optional<std::size_t> tag_with(std::string_view text, std::size_t at, std::string_view open,
                                            const std::vector<std::string_view> &names, const bool optional_slash) {
            if (!text.substr(at).starts_with(open)) {
                return std::nullopt;
            }
            for (const auto &name : names) {
                const std::size_t after = at + open.size();
                if (!starts_with_ci(text, after, name)) {
                    continue;
                }
                std::size_t p = after + name.size();
                if (p < text.size() && text[p] == ' ') {
                    p = element_content_end(text, p + 1);
                    // (?: content)? is greedy: the content may end in the optional '/'
                    if (p < text.size() && text[p] == '>') {
                        return p + 1 - at;
                    }
                    continue;
                }
                if (optional_slash && p < text.size() && text[p] == '/') {
                    ++p;
                }
                if (p < text.size() && text[p] == '>') {
                    return p + 1 - at;
                }
            }
            return std::nullopt;
        }

        std::optional<token> next_token(std::string_view text, std::size_t at) {
            const std::string_view rest = text.substr(at);
            // (<%-?=?)(.*?)(-?%>)
            if (rest.starts_with("<%")) {
                std::size_t open = 2;
                if (open < rest.size() && rest[open] == '-') {
                    ++open;
                }
                if (open < rest.size() && rest[open] == '=') {
                    ++open;
                }
                if (const auto close = rest.find("%>", open); close != std::string_view::npos) {
                    const std::size_t code_end = close > open && rest[close - 1] == '-' ? close - 1 : close;
                    return token{kind::embed, {rest.substr(0, open), rest.substr(open, code_end - open), rest.substr(code_end, close + 2 - code_end)}, close + 2};
                }
            }
            // <!--\[.*?\]>   <!\[.*?\]-->   <!--.*?-->   <!.*?>
            if (rest.starts_with("<!--[")) {
                if (const auto close = rest.find("]>", 5); close != std::string_view::npos) {
                    return token{kind::open_ie_cc, {rest.substr(0, close + 2)}, close + 2};
                }
            }
            if (rest.starts_with("<![")) {
                if (const auto close = rest.find("]-->", 3); close != std::string_view::npos) {
                    return token{kind::close_ie_cc, {rest.substr(0, close + 4)}, close + 4};
                }
            }
            if (rest.starts_with("<!--")) {
                if (const auto close = rest.find("-->", 4); close != std::string_view::npos) {
                    return token{kind::standalone, {rest.substr(0, close + 3)}, close + 3};
                }
            }
            if (rest.starts_with("<!")) {
                if (const auto close = rest.find('>', 2); close != std::string_view::npos) {
                    return token{kind::standalone, {rest.substr(0, close + 1)}, close + 1};
                }
            }
            // (<script ...>)(.*?)(</script>), and style, pre, textarea
            for (const auto &[name, type] : {std::pair{std::string_view("script"), kind::foreign_block},
                                             std::pair{std::string_view("style"), kind::foreign_block},
                                             std::pair{std::string_view("pre"), kind::preformatted_block},
                                             std::pair{std::string_view("textarea"), kind::preformatted_block}}) {
                if (rest.size() > name.size() && rest[0] == '<' && starts_with_ci(rest, 1, name)) {
                    const std::size_t open_end = element_content_end(rest, 1 + name.size());
                    if (open_end < rest.size()) {
                        const std::string closing = "</" + std::string(name) + ">";
                        if (const auto close = find_ci(rest, closing, open_end + 1); close != std::string_view::npos) {
                            return token{type, {rest.substr(0, open_end + 1), rest.substr(open_end + 1, close - open_end - 1),
                                                rest.substr(close, closing.size())}, close + closing.size()};
                        }
                    }
                }
            }
            if (const auto length = tag_with(text, at, "<", void_elements, true)) {
                return token{kind::standalone, {rest.substr(0, *length)}, *length};
            }
            if (rest.starts_with("</")) {
                for (const auto &name : block_elements) {
                    if (starts_with_ci(rest, 2, name) && 2 + name.size() < rest.size() && rest[2 + name.size()] == '>') {
                        return token{kind::close_block, {rest.substr(0, 3 + name.size())}, 3 + name.size()};
                    }
                }
            }
            if (const auto length = tag_with(text, at, "<", block_elements, false)) {
                return token{kind::open_block, {rest.substr(0, *length)}, *length};
            }
            if (rest.starts_with("</")) {
                if (const auto close = element_content_end(rest, 2); close < rest.size()) {
                    return token{kind::close_element, {rest.substr(0, close + 1)}, close + 1};
                }
            }
            // <ELEMENT_CONTENT[^/]>: up to a '>' that doesn't follow a '/'
            if (rest.starts_with("<")) {
                if (const auto close = element_content_end(rest, 1); close < rest.size() && close >= 2 && rest[close - 1] != '/') {
                    return token{kind::open_element, {rest.substr(0, close + 1)}, close + 1};
                }
                // <[\w\-]+(?: content)?/>
                std::size_t p = 1;
                while (p < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[p])) || rest[p] == '_' || rest[p] == '-')) {
                    ++p;
                }
                if (p > 1) {
                    if (p < rest.size() && rest[p] == ' ') {
                        p = element_content_end(rest, p + 1);
                        if (p < rest.size() && rest[p - 1] == '/') {
                            return token{kind::standalone, {rest.substr(0, p + 1)}, p + 1};
                        }
                    } else if (rest.substr(p).starts_with("/>")) {
                        return token{kind::standalone, {rest.substr(0, p + 2)}, p + 2};
                    }
                }
            }
            // (\s*\r?\n\s*)+: whitespace with a line break in it
            {
                std::size_t p = 0;
                while (p < rest.size() && std::isspace(static_cast<unsigned char>(rest[p]))) {
                    ++p;
                }
                if (rest.substr(0, p).find('\n') != std::string_view::npos) {
                    return token{kind::new_lines, {rest.substr(0, p)}, p};
                }
            }
            // [^<\n]+
            std::size_t p = 0;
            while (p < rest.size() && rest[p] != '<' && rest[p] != '\n') {
                ++p;
            }
            if (p > 0) {
                return token{kind::text, {rest.substr(0, p)}, p};
            }
            return std::nullopt;
        }

        std::string trim(std::string_view s) {
            const auto first = s.find_first_not_of(" \t\n\r\f\v");
            if (first == std::string_view::npos) {
                return {};
            }
            return std::string(s.substr(first, s.find_last_not_of(" \t\n\r\f\v") - first + 1));
        }

        // htmlbeautifier's Builder, with its defaults: two spaces a level, no blank lines kept
        class builder {
        public:
            std::string output;

            void handle(const token &t) {
                switch (t.type) {
                    case kind::embed: emit(std::string(t.parts[0]) + std::string(t.parts[1]) + std::string(t.parts[2])); break;
                    case kind::open_ie_cc:
                        emit(t.parts[0]);
                        ie_cc_levels.push_back(level);
                        ++level;
                        break;
                    case kind::close_ie_cc:
                        if (!ie_cc_levels.empty()) {
                            level = ie_cc_levels.back();
                            ie_cc_levels.pop_back();
                        }
                        emit(t.parts[0]);
                        break;
                    case kind::standalone:
                        emit(t.parts[0]);
                        if (t.parts[0].size() > 3 && t.parts[0].starts_with("<br") &&
                            !(std::isalnum(static_cast<unsigned char>(t.parts[0][3])) || t.parts[0][3] == '_')) {
                            new_line = true;
                        }
                        break;
                    case kind::foreign_block:
                        emit(t.parts[0]);
                        if (!trim(t.parts[1]).empty()) {
                            reindent(t.parts[1]);
                        }
                        emit(t.parts[2]);
                        break;
                    case kind::preformatted_block:
                        new_line = true;
                        emit(std::string(t.parts[0]) + std::string(t.parts[1]) + std::string(t.parts[2]));
                        new_line = true;
                        break;
                    case kind::close_block:
                        outdent();
                        emit(t.parts[0]);
                        new_line = true;
                        break;
                    case kind::open_block:
                        new_line = true;
                        emit(t.parts[0]);
                        ++level;
                        break;
                    case kind::close_element:
                        outdent();
                        emit(t.parts[0]);
                        break;
                    case kind::open_element:
                        emit(t.parts[0]);
                        ++level;
                        break;
                    case kind::new_lines: new_line = true; break;
                    case kind::text: emit(t.parts[0]); break;
                }
            }

        private:
            int level = 0;
            bool new_line = false;
            bool empty = true;
            std::vector<int> ie_cc_levels;

            void outdent() { level = std::max(level - 1, 0); }

            void emit(std::string_view text) {
                if (new_line && !empty) {
                    output += '\n';
                }
                if (new_line && !trim(text).empty()) {
                    output.append(static_cast<std::size_t>(level) * 2, ' ');
                }
                output += text;
                new_line = false;
                empty = false;
            }

            // A <script> or <style>'s contents, a level in, without the indentation its first line had
            void reindent(std::string_view code) {
                std::string indentation;
                for (std::size_t start = 0; start <= code.size();) {
                    const auto end = std::min(code.find('\n', start), code.size());
                    const std::string_view line = code.substr(start, end - start);
                    if (!trim(line).empty()) {
                        indentation = std::string(line.substr(0, line.find_first_not_of(" \t\r\f\v")));
                        break;
                    }
                    start = end + 1;
                }
                const std::string body = trim(code);
                ++level;
                new_line = true;
                for (std::size_t start = 0; start < body.size();) {
                    const auto end = std::min(body.find('\n', start), body.size());
                    std::string line(body.substr(start, end - start));
                    line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);
                    if (!indentation.empty() && line.starts_with(indentation)) {
                        line.erase(0, indentation.size());
                    }
                    emit(line);
                    new_line = true;
                    start = end + 1;
                }
                outdent();
            }
        };
    }

    std::string beautify(std::string_view html) {
        builder b;
        for (std::size_t at = 0; at < html.size();) {
            const auto t = next_token(html, at);
            if (!t) {
                // htmlbeautifier gives up here ("Unmatched sequence"); keep the rest as it is
                b.output += html.substr(at);
                break;
            }
            b.handle(*t);
            at += t->length;
        }
        return b.output;
    }
}
