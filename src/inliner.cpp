#include "inliner.h"

#include <algorithm>
#include <map>
#include <memory>
#include <regex>
#include <set>

namespace sc::bootstrap_email {
    namespace {
        // A stylesheet's rules, one selector each, as premailer goes through them (each_selector)
        struct selector_rule {
            std::string selector; // as premailer matches it
            int specificity;
            css::declarations block;
            std::vector<std::string> media;
            bool unmergeable;
        };

        std::vector<selector_rule> selector_rules(const std::vector<css::rule> &rules) {
            std::vector<selector_rule> list;
            for (const auto &r : rules) {
                // Each selector gets the rule's declarations as written out, read again (as premailer passes them on)
                const css::declarations block = css::parse_declarations(r.block.to_string());
                for (const auto &original : r.selectors) {
                    const std::string selector = css::premailer_selector(original);
                    list.push_back({selector, css::specificity(original), block, r.media, css::unmergeable(selector, r.media)});
                }
            }
            return list;
        }

        // bootstrap-email's own stylesheet, read once
        const std::vector<selector_rule> &stylesheet_rules(std::string_view stylesheet) {
            static const std::vector<selector_rule> rules = selector_rules(css::parse_stylesheet(stylesheet));
            return rules;
        }

        // The properties premailer also writes as HTML attributes, for mail clients that ignore CSS
        const std::map<std::string, std::vector<std::pair<std::string, std::string>>, std::less<>> &related_attributes() {
            static const std::map<std::string, std::vector<std::pair<std::string, std::string>>, std::less<>> related = {
                {"h1", {{"text-align", "align"}}}, {"h2", {{"text-align", "align"}}}, {"h3", {{"text-align", "align"}}},
                {"h4", {{"text-align", "align"}}}, {"h5", {{"text-align", "align"}}}, {"h6", {{"text-align", "align"}}},
                {"p", {{"text-align", "align"}}}, {"div", {{"text-align", "align"}}}, {"blockquote", {{"text-align", "align"}}},
                {"body", {{"background-color", "bgcolor"}}},
                {"table", {{"-premailer-align", "align"}, {"background-color", "bgcolor"}, {"background-image", "background"},
                           {"-premailer-width", "width"}, {"-premailer-height", "height"}, {"-premailer-cellpadding", "cellpadding"},
                           {"-premailer-cellspacing", "cellspacing"}}},
                {"tr", {{"text-align", "align"}, {"background-color", "bgcolor"}, {"-premailer-height", "height"}}},
                {"th", {{"text-align", "align"}, {"background-color", "bgcolor"}, {"vertical-align", "valign"},
                        {"-premailer-width", "width"}, {"-premailer-height", "height"}}},
                {"td", {{"text-align", "align"}, {"background-color", "bgcolor"}, {"vertical-align", "valign"},
                        {"-premailer-width", "width"}, {"-premailer-height", "height"}}},
                {"img", {{"float", "align"}, {"-premailer-width", "width"}, {"-premailer-height", "height"}}},
            };
            return related;
        }

        // rgb(1, 2, 3) as hex digits (without '#', as premailer writes them); other colours as they are
        std::string ensure_hex(const std::string &colour) {
            static const std::regex rgb(R"(rgb\(\s*(\d{1,3})(?:\s*,\s*|\s+)(\d{1,3})(?:\s*,\s*|\s+)(\d{1,3})\s*(?:/\s*\d*\.?\d*%?)?\s*\))");
            std::smatch match;
            if (!std::regex_search(colour, match, rgb)) {
                return colour;
            }
            std::string hex;
            for (int i = 1; i <= 3; ++i) {
                char part[8];
                std::snprintf(part, sizeof part, "%02X", std::stoi(match.str(i)));
                hex += part;
            }
            return hex;
        }

        std::vector<std::string> classes_of(dom::node *element) {
            std::vector<std::string> classes;
            const std::string list = dom::attribute(element, "class").value_or("");
            for (std::size_t i = 0; i < list.size();) {
                while (i < list.size() && std::isspace(static_cast<unsigned char>(list[i]))) {
                    ++i;
                }
                std::size_t end = i;
                while (end < list.size() && !std::isspace(static_cast<unsigned char>(list[end]))) {
                    ++end;
                }
                if (end > i) {
                    classes.push_back(list.substr(i, end - i));
                }
                i = end;
            }
            return classes;
        }

        // The class, id or element name a selector's last part needs, to find the elements worth matching it against
        struct selector_key {
            char kind = 0; // '.', '#', 'e' (element), or 0 when there's none to go by
            std::string name;
        };

        selector_key key_of(std::string_view selector) {
            // The last compound selector: after the last combinator outside brackets and parentheses
            std::size_t start = 0;
            int depth = 0;
            for (std::size_t i = 0; i < selector.size(); ++i) {
                const char c = selector[i];
                if (c == '[' || c == '(') {
                    ++depth;
                } else if (c == ']' || c == ')') {
                    --depth;
                } else if (depth == 0 && (c == ' ' || c == '>' || c == '+' || c == '~')) {
                    start = i + 1;
                }
            }
            const std::string_view last = selector.substr(start);
            if (last.find('\\') != std::string_view::npos || last.find(':') != std::string_view::npos) {
                return {};
            }
            auto word = [&](std::size_t from) {
                std::size_t end = from;
                while (end < last.size() && (std::isalnum(static_cast<unsigned char>(last[end])) || last[end] == '-' || last[end] == '_')) {
                    ++end;
                }
                return std::string(last.substr(from, end - from));
            };
            for (std::size_t i = 0; i < last.size(); ++i) {
                if (last[i] == '[') {
                    i = last.find(']', i);
                    if (i == std::string_view::npos) {
                        return {};
                    }
                } else if (last[i] == '.' || last[i] == '#') {
                    if (const std::string name = word(i + 1); !name.empty()) {
                        return {last[i], name};
                    }
                }
            }
            if (!last.empty() && std::isalpha(static_cast<unsigned char>(last[0]))) {
                return {'e', word(0)};
            }
            return {};
        }

        // The classes and ids a selector needs some element to have (those outside :not() and the like, and outside
        // attribute selectors)
        std::vector<std::pair<char, std::string>> required_names(std::string_view selector) {
            std::vector<std::pair<char, std::string>> names;
            int depth = 0;
            for (std::size_t i = 0; i < selector.size(); ++i) {
                const char c = selector[i];
                if (c == '\\') {
                    return {}; // escaped names: not worth reading here
                }
                if (c == '[' || c == '(') {
                    ++depth;
                } else if (c == ']' || c == ')') {
                    --depth;
                } else if (depth == 0 && (c == '.' || c == '#')) {
                    std::size_t end = i + 1;
                    while (end < selector.size() &&
                           (std::isalnum(static_cast<unsigned char>(selector[end])) || selector[end] == '-' || selector[end] == '_')) {
                        ++end;
                    }
                    if (end > i + 1) {
                        names.emplace_back(c, std::string(selector.substr(i + 1, end - i - 1)));
                    }
                    i = end - 1;
                }
            }
            return names;
        }

        // The elements by class, id and name, to match each selector only against those it could match
        struct element_index {
            std::map<std::string, std::vector<dom::node *>, std::less<>> by_class, by_id, by_name;
            std::vector<dom::node *> all;

            explicit element_index(const dom::document &doc) {
                std::vector<dom::node *> stack = {doc.html()};
                while (!stack.empty()) {
                    dom::node *n = stack.back();
                    stack.pop_back();
                    if (!dom::is_element(n)) {
                        continue;
                    }
                    all.push_back(n);
                    by_name[dom::name(n)].push_back(n);
                    for (const auto &c : classes_of(n)) {
                        auto &list = by_class[c];
                        if (list.empty() || list.back() != n) {
                            list.push_back(n);
                        }
                    }
                    if (const auto id = dom::attribute(n, "id")) {
                        by_id[*id].push_back(n);
                    }
                    auto children = dom::children(n);
                    stack.insert(stack.end(), children.rbegin(), children.rend());
                }
            }

            // nullptr: match the whole document
            const std::vector<dom::node *> *candidates(const selector_key &key) const {
                static const std::vector<dom::node *> none;
                const auto &map = key.kind == '.' ? by_class : key.kind == '#' ? by_id : by_name;
                if (key.kind == 0) {
                    return nullptr;
                }
                const auto found = map.find(key.name);
                return found == map.end() ? &none : &found->second;
            }
        };
    }

    inliner::inliner(dom::document &document, const std::string_view css) : doc(document), stylesheet(css) {
        // The page's own <style> tags (and <link rel="stylesheet">, which premailer would fetch), unless
        // data-premailer="ignore", read and taken out
        for (dom::node *tag : doc.select(R"(link[rel="stylesheet"]:not([data-premailer="ignore"]), style:not([data-premailer="ignore"]))")) {
            if (dom::name(tag) == "style") {
                for (auto &r : css::parse_stylesheet(dom::text(tag))) {
                    rules.push_back(std::move(r));
                }
            }
            dom::remove(tag);
        }
    }

    void inliner::inline_css() {
        const std::vector<selector_rule> page = selector_rules(rules);

        // Style attributes already there go first, at the specificity of an inline style
        std::map<dom::node *, std::vector<std::pair<int, std::string>>> blocks;
        std::vector<css::rule> unmergeable;
        for (dom::node *element : doc.select("[style]")) {
            const std::string style = dom::attribute(element, "style").value_or("");
            blocks[element];
            if (!style.empty()) {
                blocks[element].emplace_back(1000, style);
            }
        }

        const element_index index(doc);
        auto apply = [&](const selector_rule &r) {
            if (r.unmergeable) {
                css::rule kept;
                kept.selectors = {r.selector};
                kept.block = r.block;
                kept.media = r.media;
                unmergeable.push_back(std::move(kept));
                return;
            }
            for (const auto &[kind, name] : required_names(r.selector)) {
                const auto &map = kind == '.' ? index.by_class : index.by_id;
                if (!map.contains(name)) {
                    return; // no element has it, so nothing matches
                }
            }
            std::vector<dom::node *> matched;
            if (const auto *candidates = index.candidates(key_of(r.selector))) {
                if (candidates->empty()) {
                    return;
                }
                matched = doc.filter(*candidates, r.selector);
            } else {
                matched = doc.select(r.selector);
            }
            const std::string declarations = r.block.to_string();
            for (dom::node *element : matched) {
                if (!dom::is_element(element) || dom::name(element) == "head" || dom::name(dom::parent(element)) == "head") {
                    continue;
                }
                if (!dom::attribute(element, "style")) {
                    dom::set_attribute(element, "style", ""); // where premailer adds it, before attributes added below
                }
                blocks[element].emplace_back(r.specificity, declarations);
            }
        };
        for (const auto &r : stylesheet_rules(stylesheet)) {
            apply(r);
        }
        for (const auto &r : page) {
            apply(r);
        }

        for (dom::node *script : doc.select("script")) {
            dom::remove(script);
        }

        // Each element's rules folded into its style attribute, in document order
        for (dom::node *element : index.all) {
            const auto found = blocks.find(element);
            if (found == blocks.end()) {
                continue;
            }
            std::vector<std::pair<int, css::declarations>> sets;
            for (const auto &[specificity, block] : found->second) {
                sets.emplace_back(specificity, css::parse_declarations(block));
            }
            css::declarations merged = css::merge(std::move(sets));
            merged.expand_shorthands();

            const auto related = related_attributes().find(dom::name(element));
            if (related != related_attributes().end()) {
                for (const auto &[property, html_attribute] : related->second) {
                    const css::value *v = merged.get(property);
                    if (v != nullptr && !dom::attribute(element, html_attribute)) {
                        std::string value = v->to_string() + ";";
                        static const std::regex url(R"x(url\((['"])(.*?)\1\))x");
                        static const std::regex important(R"(;$|\s*!important)");
                        static const std::regex pixels(R"((\d+)px)");
                        value = std::regex_replace(value, url, "$2");
                        value = std::regex_replace(value, important, "");
                        value.erase(0, value.find_first_not_of(" \t\n\r\f\v"));
                        value.erase(value.find_last_not_of(" \t\n\r\f\v") + 1);
                        if (html_attribute == "width" || html_attribute == "height") {
                            value = std::regex_replace(value, pixels, "$1");
                        }
                        if (property.ends_with("color")) {
                            value = ensure_hex(value);
                        }
                        dom::set_attribute(element, html_attribute, value);
                    }
                    merged.erase(property);
                }
            }
            merged.create_shorthands();
            dom::set_attribute(element, "style", merged.to_string());
        }

        for (dom::node *element : doc.select("[contenteditable]")) {
            dom::remove_attribute(element, "contenteditable");
        }

        // What couldn't be inlined, in a <style> at the end of the head
        if (const std::string styles = css::to_stylesheet(unmergeable); !styles.empty()) {
            dom::node *style = doc.create_element("style");
            dom::append(style, doc.create_text(styles));
            dom::append(doc.head(), style);
        }

        // Nokogiri's to_html (premailer calls it) gives a head without one a Content-Type <meta>, first in the head
        if (doc.first(R"(head meta[http-equiv="Content-Type" i])") == nullptr) {
            for (dom::node *meta : doc.parse(R"(<meta http-equiv="Content-Type" content="text/html; charset=utf-8">)", "head")) {
                dom::prepend(doc.head(), meta);
            }
        }
    }
}
