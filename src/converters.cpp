#include "converters.h"

#include "resources.h"

#include <algorithm>
#include <regex>

namespace sc::bootstrap_email {
    namespace {
        std::string strip(std::string_view s) {
            const auto first = s.find_first_not_of(" \t\n\r\f\v");
            if (first == std::string_view::npos) {
                return {};
            }
            return std::string(s.substr(first, s.find_last_not_of(" \t\n\r\f\v") - first + 1));
        }

        // Ruby's split.join(' '): words with one space between
        std::string squish(std::string_view s) {
            std::string out;
            for (std::size_t i = 0; i < s.size();) {
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
                    ++i;
                }
                std::size_t end = i;
                while (end < s.size() && !std::isspace(static_cast<unsigned char>(s[end]))) {
                    ++end;
                }
                if (end > i) {
                    out += (out.empty() ? "" : " ") + std::string(s.substr(i, end - i));
                }
                i = end;
            }
            return out;
        }

        std::vector<std::string> words(std::string_view s) {
            std::vector<std::string> list;
            const std::string squished = squish(s);
            for (std::size_t start = 0; start < squished.size();) {
                const auto end = std::min(squished.find(' ', start), squished.size());
                list.push_back(squished.substr(start, end - start));
                start = end + 1;
            }
            return list;
        }

        std::string class_of(dom::node *n) { return dom::attribute(n, "class").value_or(""); }

        bool margin_top(dom::node *n) {
            static const std::regex top(R"(m[ty]{1}-(lg-)?\d+)");
            return std::regex_search(class_of(n), top);
        }
        bool margin_bottom(dom::node *n) {
            static const std::regex bottom(R"(m[by]{1}-(lg-)?\d+)");
            return std::regex_search(class_of(n), bottom);
        }
        bool margin(dom::node *n) { return margin_top(n) || margin_bottom(n); }

        void add_class(dom::node *n, std::string_view name) {
            dom::set_attribute(n, "class", squish(class_of(n) + " " + std::string(name)));
        }

        // A template, parsed: its nodes, and the comment standing where its contents go
        struct built {
            std::vector<dom::node *> nodes;
            dom::node *slot = nullptr;

            dom::node *first_element() const {
                const auto found = std::ranges::find_if(nodes, dom::is_element);
                return found == nodes.end() ? nullptr : *found;
            }
        };

        constexpr std::string_view slot_marker = "sc-bootstrap-email-contents";

        class converter {
        public:
            explicit converter(dom::document &d) : doc(d) {}

            // Base#template: the file with its classes, the contents' place kept for fill()
            built make(std::string_view file, std::string_view classes) {
                std::string text(resource("templates/" + std::string(file) + ".html"));
                if (!text.empty()) {
                    text.pop_back(); // Ruby's chop: the trailing newline
                }
                auto put = [&](std::string_view key, std::string_view value) {
                    if (const auto at = text.find(key); at != std::string::npos) {
                        text.replace(at, key.size(), value);
                    }
                };
                put("{{ classes }}", squish(classes));
                put("{{ contents }}", "<!--" + std::string(slot_marker) + "-->");
                const std::string_view context = file == "td" ? "tr" : file == "tr" ? "tbody" : "div";
                built result{doc.parse(text, context)};
                for (dom::node *top : result.nodes) {
                    if ((result.slot = find_slot(top)) != nullptr) {
                        break;
                    }
                }
                return result;
            }

            // The template's contents: nodes moved into its place (and the place taken out)
            static void fill(const built &b, const std::vector<dom::node *> &contents) {
                if (b.slot == nullptr) {
                    return;
                }
                for (dom::node *n : contents) {
                    dom::insert_before(b.slot, n);
                }
                dom::remove(b.slot);
            }

            void fill_text(const built &b, std::string_view text) { fill(b, {doc.create_text(text)}); }

            // The template's nodes before the element, which then goes into it (fill) or away (replace)
            static void put_before(dom::node *element, const built &b) {
                for (dom::node *n : b.nodes) {
                    dom::insert_before(element, n);
                }
            }

            // Node#replace: the template's nodes instead of the element
            static void replace(dom::node *element, const built &b) {
                put_before(element, b);
                dom::remove(element);
            }

            // Base#each_node: the deepest first, so an element is changed before what's around it
            std::vector<dom::node *> each(std::string_view selector) const {
                std::vector<dom::node *> found = doc.select(selector);
                std::ranges::stable_sort(found, {}, dom::depth);
                std::ranges::reverse(found);
                return found;
            }

            void body() {
                dom::node *b = doc.body();
                const built t = make("body", class_of(b) + " body");
                fill(t, dom::children(b));
                for (dom::node *n : t.nodes) {
                    dom::append(b, n);
                }
            }

            void block() {
                for (dom::node *n : each("block, .to-table")) {
                    std::vector<std::string> names = words(class_of(n));
                    names.emplace_back("to-table");
                    std::vector<std::string> unique;
                    for (const auto &name : names) {
                        if (std::ranges::find(unique, name) == unique.end()) {
                            unique.push_back(name);
                        }
                    }
                    std::string classes;
                    for (const auto &name : unique) {
                        classes += (classes.empty() ? "" : " ") + name;
                    }
                    wrap_children(n, "table", classes);
                }
            }

            // The element itself, without its classes, inside a template that has them (buttons, badges, alerts)
            void wrap_self(std::string_view selector, std::string_view file) {
                for (dom::node *n : each(selector)) {
                    const std::string classes = class_of(n);
                    dom::remove_attribute(n, "class");
                    const built t = make(file, classes);
                    put_before(n, t);
                    fill(t, {n});
                }
            }

            // The element's children inside a template with its classes, instead of the element
            void wrap_children(dom::node *n, std::string_view file, std::string_view classes) {
                const built t = make(file, classes);
                fill(t, dom::children(n));
                replace(n, t);
            }

            void card() {
                for (dom::node *n : each(".card")) {
                    wrap_children(n, "table", class_of(n));
                }
                for (dom::node *n : each(".card-body")) {
                    wrap_children(n, "table", class_of(n));
                }
            }

            void hr() {
                for (dom::node *n : each("hr")) {
                    const built t = make("table", std::string(margin(n) ? "" : "my-5") + " hr " + class_of(n));
                    fill(t, {});
                    replace(n, t);
                }
            }

            void container() {
                for (dom::node *n : each(".container")) {
                    wrap_children(n, "container", class_of(n));
                }
                for (dom::node *n : each(".container-fluid")) {
                    wrap_children(n, "table", class_of(n));
                }
            }

            void grid() {
                for (dom::node *n : each(".row")) {
                    const auto children = dom::element_children(n);
                    if (std::ranges::any_of(children, [](dom::node *c) { return class_of(c).find("col-lg-") != std::string::npos; })) {
                        add_class(n, "row-responsive");
                    }
                    const built outer = make("div", class_of(n));
                    const built inner = make("table-to-tr", "");
                    fill(inner, dom::children(n));
                    fill(outer, inner.nodes);
                    replace(n, outer);
                }
                for (dom::node *n : each("*[class*=col]")) {
                    wrap_children(n, "td", class_of(n));
                }
            }

            void stack() {
                for (dom::node *n : each(".stack-row")) {
                    std::vector<dom::node *> cells;
                    for (dom::node *child : dom::element_children(n)) {
                        const built cell = make("td", "stack-cell");
                        fill(cell, {child});
                        cells.insert(cells.end(), cell.nodes.begin(), cell.nodes.end());
                    }
                    const built t = make("table-to-tr", class_of(n));
                    fill(t, cells);
                    replace(n, t);
                }
                for (dom::node *n : each(".stack-col")) {
                    std::vector<dom::node *> rows;
                    for (dom::node *child : dom::element_children(n)) {
                        const built row = make("tr", "stack-cell");
                        fill(row, {child});
                        rows.insert(rows.end(), row.nodes.begin(), row.nodes.end());
                    }
                    const built t = make("table-to-tbody", class_of(n));
                    fill(t, rows);
                    replace(n, t);
                }
            }

            void color() {
                for (dom::node *n : each("*[class*=bg-]")) {
                    if (dom::name(n) != "div") {
                        continue;
                    }
                    const std::string classes = class_of(n) + " w-full";
                    dom::remove_attribute(n, "class");
                    wrap_children(n, "table", classes);
                }
            }

            void spacing() {
                static const std::regex space_y(R"(space-y-((lg-)?\d+))");
                for (dom::node *n : each("*[class*=space-y-]")) {
                    std::smatch match;
                    const std::string classes = class_of(n);
                    if (!std::regex_search(classes, match, space_y)) {
                        continue;
                    }
                    const std::string spacer = match.str(1);
                    // ./*[position() < last()] | ./tbody/tr/td/*[position() < last()], in document order
                    std::vector<dom::node *> targets;
                    auto all_but_last = [&](dom::node *parent) {
                        auto list = dom::element_children(parent);
                        if (!list.empty()) {
                            list.pop_back();
                        }
                        return list;
                    };
                    std::vector<dom::node *> direct = all_but_last(n);
                    std::vector<dom::node *> nested;
                    for (dom::node *tbody : dom::element_children(n)) {
                        if (dom::name(tbody) != "tbody") {
                            continue;
                        }
                        for (dom::node *tr : dom::element_children(tbody)) {
                            if (dom::name(tr) != "tr") {
                                continue;
                            }
                            for (dom::node *td : dom::element_children(tr)) {
                                if (dom::name(td) == "td") {
                                    const auto list = all_but_last(td);
                                    nested.insert(nested.end(), list.begin(), list.end());
                                }
                            }
                        }
                    }
                    targets = direct;
                    targets.insert(targets.end(), nested.begin(), nested.end());
                    for (dom::node *child : targets) {
                        if (!margin_bottom(child)) {
                            add_class(child, "mb-" + spacer);
                        }
                    }
                }
            }

            void margins() {
                static const std::regex top(R"(m[ty]{1}-(lg-)?(\d+))");
                static const std::regex bottom(R"(m[by]{1}-(lg-)?(\d+))");
                static const std::regex any(R"((m[tby]{1}-(lg-)?\d+))");
                for (dom::node *n : each("*[class^='my-'], *[class^='mt-'], *[class^='mb-'], *[class*=' my-'], *[class*=' mt-'], *[class*=' mb-']")) {
                    const std::string classes = class_of(n);
                    std::smatch top_match, bottom_match;
                    const bool has_top = std::regex_search(classes, top_match, top);
                    const bool has_bottom = std::regex_search(classes, bottom_match, bottom);
                    dom::set_attribute(n, "class", strip(std::regex_replace(classes, any, "")));
                    if (has_top) {
                        const built spacer = make("div", "s-" + std::regex_replace(top_match.str(0), std::regex("m[ty]{1}-"), ""));
                        fill(spacer, {});
                        for (dom::node *s : spacer.nodes) {
                            dom::insert_before(n, s);
                        }
                    }
                    if (has_bottom) {
                        const built spacer = make("div", "s-" + std::regex_replace(bottom_match.str(0), std::regex("m[by]{1}-"), ""));
                        fill(spacer, {});
                        dom::node *after = n;
                        for (dom::node *s : spacer.nodes) {
                            dom::insert_after(after, s);
                            after = s;
                        }
                    }
                }
            }

            void spacer() {
                static const std::regex size(R"(^s(-lg)?-\d+$)");
                for (dom::node *n : each("*[class*=s-]")) {
                    const auto names = words(class_of(n));
                    if (std::ranges::none_of(names, [](const std::string &name) { return std::regex_search(name, size); })) {
                        continue;
                    }
                    const built t = make("table", class_of(n) + " w-full");
                    fill_text(t, " ");
                    replace(n, t);
                }
            }

            void align() {
                for (const std::string type : {"left", "center", "right"}) {
                    const std::string full = "ax-" + type;
                    for (dom::node *n : each("." + full)) {
                        dom::node *target = n;
                        if (dom::name(n) != "table" && dom::name(n) != "td") {
                            std::string classes = class_of(n);
                            if (const auto at = classes.find(full); at != std::string::npos) {
                                classes.erase(at, full.size());
                            }
                            dom::set_attribute(n, "class", strip(classes));
                            const built t = make("table", full);
                            put_before(n, t);
                            fill(t, {n});
                            target = t.first_element();
                        }
                        if (target != nullptr) {
                            dom::set_attribute(target, "align", type);
                        }
                    }
                }
            }

            void padding() {
                static const std::regex padding_class(R"((p[trblxy]?-(lg-)?\d+))");
                const char *selector = "*[class^=p-], *[class^=pt-], *[class^=pr-], *[class^=pb-], *[class^=pl-], *[class^=px-], "
                                       "*[class^=py-], *[class*=' p-'], *[class*=' pt-'], *[class*=' pr-'], *[class*=' pb-'], "
                                       "*[class*=' pl-'], *[class*=' px-'], *[class*=' py-']";
                for (dom::node *n : each(selector)) {
                    const std::string name = dom::name(n);
                    if (name == "table" || name == "td" || name == "a") {
                        continue;
                    }
                    const std::string classes = class_of(n);
                    std::string found;
                    for (std::sregex_iterator it(classes.begin(), classes.end(), padding_class), end; it != end; ++it) {
                        found += (found.empty() ? "" : " ") + it->str();
                    }
                    dom::set_attribute(n, "class", strip(std::regex_replace(classes, padding_class, "")));
                    const built t = make("table", found);
                    put_before(n, t);
                    fill(t, {n});
                }
            }

            void preview_text() {
                dom::node *preview = doc.first("preview");
                if (preview == nullptr) {
                    return;
                }
                std::string content = dom::text(preview);
                std::size_t characters = 0;
                for (const char c : content) {
                    characters += (static_cast<unsigned char>(c) & 0xc0) != 0x80;
                }
                for (std::size_t i = characters; i < 278; ++i) {
                    content += "͏ ‌   ";
                }
                const built t = make("div", "preview");
                fill_text(t, content);
                dom::remove(preview);
                for (auto it = t.nodes.rbegin(); it != t.nodes.rend(); ++it) {
                    dom::prepend(doc.body(), *it);
                }
            }

            void table() {
                for (dom::node *n : each("table")) {
                    dom::set_attribute(n, "border", "0");
                    dom::set_attribute(n, "cellpadding", "0");
                    dom::set_attribute(n, "cellspacing", "0");
                }
            }

        private:
            dom::document &doc;

            static dom::node *find_slot(dom::node *n) {
                if (dom::is_comment(n) && dom::text(n) == slot_marker) {
                    return n;
                }
                for (dom::node *child : dom::children(n)) {
                    if (dom::node *found = find_slot(child)) {
                        return found;
                    }
                }
                return nullptr;
            }
        };

        std::string head_css(const dom::document &doc) {
            const std::string css(resource("css/bootstrap-head.css"));
            constexpr std::string_view marker = "/*! allow_purge_after */";
            const auto split = css.find(marker);
            const std::string defaults = split == std::string::npos ? css : css.substr(0, split);
            std::string custom = split == std::string::npos ? std::string() : css.substr(split + marker.size());

            // Each rule group after the marker (a class, then up to its first '}') is kept when one of the classes
            // its selectors start with is in the document
            auto word = [](const char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
            auto class_char = [&](const char c) { return word(c) || c == '-'; };
            std::vector<std::string> groups;
            for (std::size_t i = 0; i < custom.size();) {
                // \w*\.  then [\w-]*  then at least one character up to the first '}'
                std::size_t start = i;
                std::size_t dot = std::string::npos;
                for (std::size_t p = i; p < custom.size(); ++p) {
                    std::size_t q = p;
                    while (q < custom.size() && word(custom[q])) {
                        ++q;
                    }
                    if (q < custom.size() && custom[q] == '.') {
                        start = p;
                        dot = q;
                        break;
                    }
                    if (q > p) {
                        p = q - 1;
                    }
                }
                if (dot == std::string::npos) {
                    break;
                }
                std::size_t run = dot + 1;
                while (run < custom.size() && class_char(custom[run])) {
                    ++run;
                }
                const auto close = custom.find('}', std::max(run + 1, dot + 2));
                if (close == std::string::npos) {
                    break;
                }
                groups.push_back(custom.substr(start, close + 1 - start));
                i = close + 1;
            }
            for (const auto &group : groups) {
                bool exists = false;
                std::vector<std::string> seen;
                for (std::size_t i = 0; i < group.size() && !exists;) {
                    const auto dot = group.find('.', i);
                    if (dot == std::string::npos) {
                        break;
                    }
                    std::size_t end = dot + 1;
                    while (end < group.size() && class_char(group[end])) {
                        ++end;
                    }
                    const auto delimiter = group.find_first_of(",{\n", end);
                    if (delimiter == std::string::npos || group[delimiter] == '\n') {
                        i = dot + 1;
                        continue;
                    }
                    const std::string selector = group.substr(dot, end - dot);
                    if (std::ranges::find(seen, selector) == seen.end()) {
                        seen.push_back(selector);
                        exists = doc.first(selector) != nullptr;
                    }
                    i = delimiter + 1;
                }
                if (!exists) {
                    if (const auto at = custom.find(group); at != std::string::npos) {
                        custom.erase(at, group.size());
                    }
                }
            }
            return std::regex_replace(defaults + custom, std::regex(R"(\n\s*\n+)"), "\n");
        }
    }

    void convert(dom::document &doc) {
        converter c(doc);
        c.body();
        c.block();
        c.wrap_self(".btn", "table");
        c.wrap_self(".badge", "table-left");
        c.wrap_self(".alert", "table");
        c.card();
        c.hr();
        c.container();
        c.grid();
        c.stack();
        c.color();
        c.spacing();
        c.margins();
        c.spacer();
        c.align();
        c.padding();
        c.preview_text();
        c.table();
    }

    void configure(dom::document &doc) {
        // HeadStyle: the head's own stylesheet, for what mail clients only take from a <style>
        const std::string style = "          <style type=\"text/css\">\n            " + head_css(doc) + "\n          </style>\n";
        for (dom::node *n : doc.parse(style, "head")) {
            dom::append(doc.head(), n);
        }

        // AddMissingMetaTags: each one first in the head, unless it's there
        const std::vector<std::pair<const char *, const char *>> meta_tags = {
            {R"(meta[name="format-detection"])",
             R"(<meta name="format-detection" content="telephone=no, date=no, address=no, email=no">)"},
            {R"(meta[name="viewport"])", R"(<meta name="viewport" content="width=device-width, initial-scale=1">)"},
            {R"(meta[name="x-apple-disable-message-reformatting"])", R"(<meta name="x-apple-disable-message-reformatting">)"},
            {R"(meta[http-equiv="x-ua-compatible"])", R"(<meta http-equiv="x-ua-compatible" content="ie=edge">)"},
            {R"(meta[http-equiv="Content-Type"])", R"(<meta http-equiv="Content-Type" content="text/html; charset=utf-8">)"},
        };
        for (const auto &[query, code] : meta_tags) {
            if (doc.first(query) == nullptr) {
                const auto nodes = doc.parse(code, "head");
                for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
                    dom::prepend(doc.head(), *it);
                }
            }
        }

        // VersionComment: the bootstrap-email version this is a port of
        std::string version(resource("VERSION"));
        version = strip(version);
        const auto nodes = doc.parse("\n    <!-- Compiled with Bootstrap Email version: " + version + " -->", "head");
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
            dom::prepend(doc.head(), *it);
        }
    }
}
