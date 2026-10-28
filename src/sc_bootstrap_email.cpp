// bootstrap-email's Compiler (lib/bootstrap-email/compiler.rb): the layout when the HTML has no head, premailer's
// document with the stylesheet, the converters, the inlining, the head, and the finishing touches to the text.

#include "sc_bootstrap_email.h"

#include "beautify.h"
#include "converters.h"
#include "dom.h"
#include "inliner.h"
#include "resources.h"

#include <cctype>

namespace sc::bootstrap_email {
    namespace {
        // Whether libxml2 would give the page a <head>: it has one, or starts with what goes in one
        bool has_head(std::string_view html) {
            for (std::size_t at = html.find('<'); at != std::string_view::npos; at = html.find('<', at + 1)) {
                if (at + 1 >= html.size() || html[at + 1] == '!' || html[at + 1] == '?' || html[at + 1] == '/') {
                    continue;
                }
                std::size_t end = at + 1;
                std::string name;
                while (end < html.size() && (std::isalnum(static_cast<unsigned char>(html[end])) || html[end] == '-')) {
                    name += static_cast<char>(std::tolower(static_cast<unsigned char>(html[end++])));
                }
                if (name.empty() || name == "html") {
                    continue;
                }
                return name == "head" || name == "title" || name == "meta" || name == "link" || name == "style" ||
                       name == "base" || name == "script";
            }
            return false;
        }

        // Compiler#add_layout
        std::string with_layout(std::string_view html) {
            if (has_head(html)) {
                return std::string(html);
            }
            std::string layout(resource("layout.html"));
            constexpr std::string_view slot = "<%= contents %>";
            if (const auto at = layout.find(slot); at != std::string::npos) {
                layout.replace(at, slot.size(), html);
            }
            return layout;
        }

        // CGI.unescape: %XX and '+' as a space
        std::string unescape(std::string_view s) {
            std::string out;
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '+') {
                    out += ' ';
                } else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                           std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
                    out += static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16));
                    i += 2;
                } else {
                    out += s[i];
                }
            }
            return out;
        }

        // The first of several strings in text from at, before the end of the line: where, and which
        std::pair<std::size_t, std::size_t> find_first(std::string_view text, std::size_t at, std::size_t line_end,
                                                       std::initializer_list<std::string_view> options) {
            for (std::size_t p = at; p < line_end; ++p) {
                for (const auto &option : options) {
                    if (p + option.size() <= line_end && text.substr(p, option.size()) == option) {
                        return {p, option.size()};
                    }
                }
            }
            return {std::string_view::npos, 0};
        }

        // SupportUrlTokens: template tags in links ({{ ... }}, {% ... %}) put back as written, after libxml2
        // percent-encoded what's inside them
        std::string support_url_tokens(const std::string &html) {
            const std::initializer_list<std::string_view> open = {"%7B%7B", "{{", "%7B%", "{%"};
            const std::initializer_list<std::string_view> close = {"%7D%7D", "}}", "%%7D", "%}"};
            std::string out;
            std::size_t done = 0;
            for (std::size_t at = 0; at < html.size();) {
                // (href|src)=("|')
                std::size_t attribute = std::string::npos;
                std::size_t value = 0;
                for (std::size_t p = at; p < html.size(); ++p) {
                    for (const std::string_view name : {"href=", "src="}) {
                        if (html.compare(p, name.size(), name) == 0 && p + name.size() < html.size() &&
                            (html[p + name.size()] == '"' || html[p + name.size()] == '\'')) {
                            attribute = p;
                            value = p + name.size() + 1;
                            break;
                        }
                    }
                    if (attribute != std::string::npos) {
                        break;
                    }
                }
                if (attribute == std::string::npos) {
                    break;
                }
                const std::size_t line_end = std::min(html.find('\n', value), html.size());
                const auto [token_start, open_length] = find_first(html, value, line_end, open);
                const auto [token_end, close_length] = token_start == std::string::npos
                                                           ? std::pair<std::size_t, std::size_t>{std::string::npos, 0}
                                                           : find_first(html, token_start + open_length, line_end, close);
                const std::size_t quote = token_end == std::string::npos ? std::string::npos
                                                                         : html.find_first_of("\"'", token_end + close_length);
                if (quote == std::string::npos || quote >= line_end) {
                    at = value;
                    continue;
                }
                // The value up to that quote, each token in it unescaped
                out.append(html, done, value - done);
                std::string middle = html.substr(value, quote - value);
                std::string fixed;
                for (std::size_t p = 0; p < middle.size();) {
                    const auto [o, ol] = find_first(middle, p, middle.size(), open);
                    const auto [c, cl] = o == std::string::npos ? std::pair<std::size_t, std::size_t>{std::string::npos, 0}
                                                                : find_first(middle, o + ol, middle.size(), close);
                    if (c == std::string::npos) {
                        fixed += middle.substr(p);
                        break;
                    }
                    fixed += middle.substr(p, o - p) + unescape(middle.substr(o, c + cl - o));
                    p = c + cl;
                }
                out += fixed;
                done = quote;
                at = quote + 1;
            }
            out.append(html, done);
            return out;
        }
    }

    std::string compile(std::string_view html) {
        // An HTML5 parser puts a <tbody> around a table's rows where the HTML has none; libxml2 doesn't, and the
        // stylesheet's selectors (.table > tbody > tr) tell. The ones the HTML has are marked, the others undone.
        constexpr std::string_view mark = " data-sc-bootstrap-email-tbody";
        std::string marked = with_layout(html);
        for (std::size_t at = 0; (at = marked.find("<tbody", at)) != std::string::npos; at += 6) {
            const std::size_t after = at + 6;
            if (after < marked.size() && (marked[after] == '>' || marked[after] == '/' || std::isspace(static_cast<unsigned char>(marked[after])))) {
                marked.insert(after, mark);
            }
        }
        dom::document doc(marked);
        for (dom::node *tbody : doc.select("tbody")) {
            if (dom::attribute(tbody, mark.substr(1))) {
                dom::remove_attribute(tbody, mark.substr(1));
                continue;
            }
            for (dom::node *child : dom::children(tbody)) {
                dom::insert_before(tbody, child);
            }
            dom::remove(tbody);
        }
        inliner premailer(doc, resource("css/bootstrap-email.css"));
        convert(doc);
        premailer.inline_css();
        configure(doc);

        std::string out = support_url_tokens(doc.to_html());
        constexpr std::string_view ascii_meta = R"(<meta http-equiv="Content-Type" content="text/html; charset=US-ASCII">)";
        if (const auto at = out.find(ascii_meta); at != std::string::npos) {
            out.replace(at, ascii_meta.size(), R"(<meta http-equiv="Content-Type" content="text/html; charset=utf-8">)");
        }
        return beautify(out) + "\n";
    }

    std::string upstream_version() {
        std::string version(resource("VERSION"));
        while (!version.empty() && std::isspace(static_cast<unsigned char>(version.back()))) {
            version.pop_back();
        }
        return version;
    }
}
