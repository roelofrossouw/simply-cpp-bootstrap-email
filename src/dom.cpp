#include "dom.h"

#include <lexbor/css/css.h>
#include <lexbor/html/html.h>
#include <lexbor/selectors/selectors.h>

#include <algorithm>
#include <unordered_map>
#include <set>
#include <stdexcept>

namespace sc::bootstrap_email::dom {
    namespace {
        const lxb_char_t *bytes(std::string_view s) { return reinterpret_cast<const lxb_char_t *>(s.data()); }
        // Call lexbor first, then this with the length it set: in view(f(&length), length) the order isn't defined
        // (GCC reads length before calling f)
        std::string_view view(const lxb_char_t *data, const std::size_t length) {
            return data == nullptr ? std::string_view() : std::string_view(reinterpret_cast<const char *>(data), length);
        }

        lxb_dom_element_t *element(node *n) { return lxb_dom_interface_element(n); }

        // libxml2's HTML 4 elements (htmlTagLookup), those it writes on a line of their own, and those without content
        const std::set<std::string, std::less<>> &known_elements() {
            static const std::set<std::string, std::less<>> names = {
                "a", "abbr", "acronym", "address", "applet", "area", "b", "base", "basefont", "bdo", "big", "blockquote", "body",
                "br", "button", "caption", "center", "cite", "code", "col", "colgroup", "dd", "del", "dfn", "dir", "div", "dl",
                "dt", "em", "embed", "fieldset", "font", "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head",
                "hr", "html", "i", "iframe", "img", "input", "ins", "isindex", "kbd", "label", "legend", "li", "link", "map",
                "menu", "meta", "noframes", "noscript", "object", "ol", "optgroup", "option", "p", "param", "pre", "q", "s",
                "samp", "script", "select", "small", "span", "strike", "strong", "style", "sub", "sup", "table", "tbody", "td",
                "textarea", "tfoot", "th", "thead", "title", "tr", "tt", "u", "ul", "var"};
            return names;
        }
        const std::set<std::string, std::less<>> &inline_elements() {
            static const std::set<std::string, std::less<>> names = {
                "a", "abbr", "acronym", "applet", "b", "basefont", "bdo", "big", "br", "button", "cite", "code", "del", "dfn",
                "em", "font", "i", "iframe", "img", "input", "ins", "kbd", "label", "map", "object", "q", "s", "samp", "script",
                "select", "small", "span", "strike", "strong", "sub", "sup", "textarea", "tt", "u", "var"};
            return names;
        }
        const std::set<std::string, std::less<>> &void_elements() {
            static const std::set<std::string, std::less<>> names = {
                "area", "base", "basefont", "br", "col", "embed", "frame", "hr", "img", "input", "isindex", "link", "meta",
                "param", "source", "track", "wbr"};
            return names;
        }

        // Characters outside ASCII as decimal character references, as libxml2 writes them in a US-ASCII document
        void append_escaped(std::string &out, std::string_view text) {
            for (std::size_t i = 0; i < text.size();) {
                const auto c = static_cast<unsigned char>(text[i]);
                if (c < 0x80) {
                    switch (c) {
                        case '&': out += "&amp;"; break;
                        case '<': out += "&lt;"; break;
                        case '>': out += "&gt;"; break;
                        default: out += static_cast<char>(c);
                    }
                    ++i;
                    continue;
                }
                const int length = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
                char32_t code = length == 4 ? c & 0x07 : length == 3 ? c & 0x0f : length == 2 ? c & 0x1f : c;
                for (int k = 1; k < length && i + k < text.size(); ++k) {
                    code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3f);
                }
                out += "&#" + std::to_string(static_cast<unsigned long>(code)) + ";";
                i += static_cast<std::size_t>(length);
            }
        }

        // An address attribute: what isn't ASCII (or is a space or control character) percent-encoded, as libxml2's
        // xmlURIEscapeStr does
        std::string escape_uri(std::string_view value) {
            static constexpr std::string_view hex = "0123456789ABCDEF";
            std::string out;
            for (const char ch : value) {
                const auto c = static_cast<unsigned char>(ch);
                if (c <= 0x20 || c >= 0x7f) {
                    out += '%';
                    out += hex[c >> 4];
                    out += hex[c & 0x0f];
                } else {
                    out += ch;
                }
            }
            return out;
        }

        void write(std::string &out, node *n);

        bool is_text_like(const node *n) { return n != nullptr && n->type == LXB_DOM_NODE_TYPE_TEXT; }

        void write_element(std::string &out, node *n) {
            const std::string tag = name(n);
            out += "<" + tag;
            for (lxb_dom_attr_t *attr = lxb_dom_element_first_attribute(element(n)); attr != nullptr; attr = lxb_dom_element_next_attribute(attr)) {
                std::size_t length = 0;
                const lxb_char_t *attr_name_data = lxb_dom_attr_qualified_name(attr, &length);
                const std::string attr_name(view(attr_name_data, length));
                out += " " + attr_name;
                if (attr->value == nullptr) {
                    continue;
                }
                const lxb_char_t *value_data = lxb_dom_attr_value(attr, &length);
                const std::string value(view(value_data, length));
                const bool uri = attr_name == "href" || attr_name == "src" || attr_name == "action" || (attr_name == "name" && tag == "a");
                std::string escaped;
                append_escaped(escaped, uri ? escape_uri(value) : value);
                // libxml2's xmlBufWriteQuotedString: single quotes around a value with a double quote in it, unless it
                // has both
                if (escaped.find('"') == std::string::npos) {
                    out += "=\"" + escaped + "\"";
                } else if (escaped.find('\'') == std::string::npos) {
                    out += "='" + escaped + "'";
                } else {
                    std::string quoted;
                    for (const char c : escaped) {
                        quoted += c == '"' ? std::string("&quot;") : std::string(1, c);
                    }
                    out += "=\"" + quoted + "\"";
                }
            }
            const bool known = known_elements().contains(tag);
            const bool block = known && !inline_elements().contains(tag);
            const node *after = n->next;
            const bool parent_p = n->parent != nullptr && name(n->parent).starts_with('p');
            if (void_elements().contains(tag)) {
                out += ">";
                if (block && after != nullptr && !is_text_like(after) && !parent_p) {
                    out += "\n";
                }
                return;
            }
            out += ">";
            const node *first = n->first_child;
            const node *last = n->last_child;
            if (first != nullptr) {
                if (block && !is_text_like(first) && first != last && !tag.starts_with('p')) {
                    out += "\n";
                }
                if (tag == "style" || tag == "script") {
                    for (const node *child = first; child != nullptr; child = child->next) {
                        if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
                            out += text(const_cast<node *>(child));
                        }
                    }
                } else {
                    for (node *child = n->first_child; child != nullptr; child = child->next) {
                        write(out, child);
                    }
                }
                if (block && !is_text_like(last) && first != last && !tag.starts_with('p')) {
                    out += "\n";
                }
            }
            out += "</" + tag + ">";
            if (block && after != nullptr && !is_text_like(after) && !parent_p) {
                out += "\n";
            }
        }

        void write(std::string &out, node *n) {
            switch (n->type) {
                case LXB_DOM_NODE_TYPE_ELEMENT:
                    write_element(out, n);
                    break;
                case LXB_DOM_NODE_TYPE_TEXT:
                    append_escaped(out, text(n));
                    break;
                case LXB_DOM_NODE_TYPE_COMMENT:
                    out += "<!--" + text(n) + "-->";
                    break;
                default:
                    break;
            }
        }
    }

    // lexbor's selector parser and matcher, with each selector list parsed once: per thread, as lexbor's objects
    // aren't shared. The same few thousand selectors come back for every email; a page's own stylesheet adds its
    // own, so the cache starts over when it gets big.
    struct string_hash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    struct document::selector_engine {
        lxb_css_memory_t *memory = nullptr;
        lxb_css_parser_t *parser = nullptr;
        lxb_css_selectors_t *css_selectors = nullptr;
        lxb_selectors_t *selectors = lxb_selectors_create();
        std::unordered_map<std::string, lxb_css_selector_list_t *, string_hash, std::equal_to<>> parsed;

        selector_engine() {
            if (lxb_selectors_init(selectors) != LXB_STATUS_OK) {
                throw std::runtime_error("sc-bootstrap-email: lexbor's CSS selectors didn't start");
            }
            lxb_selectors_opt_set(selectors, LXB_SELECTORS_OPT_MATCH_FIRST);
            start_parser();
        }

        ~selector_engine() {
            stop_parser();
            lxb_selectors_destroy(selectors, true);
        }

        static selector_engine &for_thread() {
            thread_local selector_engine engine;
            return engine;
        }

        // nullptr for a selector lexbor can't read
        lxb_css_selector_list_t *list(std::string_view selector) {
            if (const auto found = parsed.find(selector); found != parsed.end()) {
                return found->second;
            }
            if (parsed.size() >= 20000) {
                stop_parser();
                start_parser();
            }
            lxb_css_selector_list_t *list = lxb_css_selectors_parse(parser, bytes(selector), selector.size());
            parsed.emplace(std::string(selector), list);
            return list;
        }

    private:
        void start_parser() {
            memory = lxb_css_memory_create();
            parser = lxb_css_parser_create();
            css_selectors = lxb_css_selectors_create();
            if (lxb_css_memory_init(memory, 4096) != LXB_STATUS_OK || lxb_css_parser_init(parser, nullptr) != LXB_STATUS_OK ||
                lxb_css_selectors_init(css_selectors) != LXB_STATUS_OK) {
                throw std::runtime_error("sc-bootstrap-email: lexbor's CSS parser didn't start");
            }
            lxb_css_parser_memory_set(parser, memory);
            lxb_css_parser_selectors_set(parser, css_selectors);
        }

        void stop_parser() {
            parsed.clear();
            lxb_css_parser_destroy(parser, true);
            lxb_css_selectors_destroy(css_selectors, true);
            lxb_css_memory_destroy(memory, true); // every list parsed with it
        }
    };

    document::document(std::string_view html) : doc(lxb_html_document_create()), engine(nullptr) {
        if (doc == nullptr || lxb_html_document_parse(doc, bytes(html), html.size()) != LXB_STATUS_OK) {
            if (doc != nullptr) {
                lxb_html_document_destroy(doc);
            }
            throw std::runtime_error("sc-bootstrap-email: the HTML couldn't be parsed");
        }
        engine = &selector_engine::for_thread();
    }

    document::~document() { lxb_html_document_destroy(doc); }

    node *document::html() const { return lxb_dom_interface_node(lxb_dom_document_element(lxb_dom_interface_document(doc))); }
    node *document::head() const { return lxb_dom_interface_node(lxb_html_document_head_element(doc)); }
    node *document::body() const { return lxb_dom_interface_node(lxb_html_document_body_element(doc)); }

    std::vector<node *> document::select(std::string_view selectors) const {
        std::vector<node *> found;
        if (const lxb_css_selector_list_t *list = engine->list(selectors)) {
            lxb_selectors_find(engine->selectors, lxb_dom_interface_node(doc), list,
                               [](lxb_dom_node_t *n, lxb_css_selector_specificity_t, void *context) -> lxb_status_t {
                                   static_cast<std::vector<node *> *>(context)->push_back(n);
                                   return LXB_STATUS_OK;
                               }, &found);
        }
        return found;
    }

    node *document::first(std::string_view selectors) const {
        const auto found = select(selectors);
        return found.empty() ? nullptr : found.front();
    }

    bool document::matches(node *n, std::string_view selectors) const {
        const lxb_css_selector_list_t *list = engine->list(selectors);
        if (list == nullptr) {
            return false;
        }
        bool matched = false;
        lxb_selectors_match_node(engine->selectors, n, list,
                                 [](lxb_dom_node_t *, lxb_css_selector_specificity_t, void *context) -> lxb_status_t {
                                     *static_cast<bool *>(context) = true;
                                     return LXB_STATUS_OK;
                                 }, &matched);
        return matched;
    }

    std::vector<node *> document::filter(const std::vector<node *> &elements, std::string_view selectors) const {
        std::vector<node *> matched;
        const lxb_css_selector_list_t *list = engine->list(selectors);
        if (list == nullptr) {
            return matched;
        }
        for (node *n : elements) {
            bool found = false;
            lxb_selectors_match_node(engine->selectors, n, list,
                                     [](lxb_dom_node_t *, lxb_css_selector_specificity_t, void *context) -> lxb_status_t {
                                         *static_cast<bool *>(context) = true;
                                         return LXB_STATUS_OK;
                                     }, &found);
            if (found) {
                matched.push_back(n);
            }
        }
        return matched;
    }

    std::vector<node *> document::parse(std::string_view html, std::string_view context) {
        node *holder = create_element(context);
        node *root = lxb_html_document_parse_fragment(doc, element(holder), bytes(html), html.size());
        std::vector<node *> parsed;
        if (root != nullptr) {
            while (node *child = root->first_child) {
                lxb_dom_node_remove_wo_events(child);
                parsed.push_back(child);
            }
            lxb_dom_node_destroy_deep(root);
        }
        lxb_dom_node_destroy_deep(holder);
        return parsed;
    }

    node *document::create_text(std::string_view text) {
        return lxb_dom_interface_node(lxb_dom_document_create_text_node(lxb_dom_interface_document(doc), bytes(text), text.size()));
    }

    node *document::create_element(std::string_view element_name) {
        return lxb_dom_interface_node(lxb_dom_document_create_element(lxb_dom_interface_document(doc), bytes(element_name),
                                                                      element_name.size(), nullptr));
    }

    std::string document::to_html() const {
        std::string out = "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n";
        for (node *child = lxb_dom_interface_node(doc)->first_child; child != nullptr; child = child->next) {
            if (child->type == LXB_DOM_NODE_TYPE_ELEMENT || child->type == LXB_DOM_NODE_TYPE_COMMENT) {
                write(out, child);
                out += "\n";
            }
        }
        return out;
    }

    bool is_element(const node *n) { return n != nullptr && n->type == LXB_DOM_NODE_TYPE_ELEMENT; }
    bool is_text(const node *n) { return n != nullptr && n->type == LXB_DOM_NODE_TYPE_TEXT; }
    bool is_comment(const node *n) { return n != nullptr && n->type == LXB_DOM_NODE_TYPE_COMMENT; }

    std::string name(const node *n) {
        if (!is_element(n)) {
            return {};
        }
        std::size_t length = 0;
        const lxb_char_t *data = lxb_dom_element_local_name(element(const_cast<node *>(n)), &length);
        return std::string(view(data, length));
    }

    std::optional<std::string> attribute(node *n, std::string_view attr_name) {
        if (!is_element(n) || !lxb_dom_element_has_attribute(element(n), bytes(attr_name), attr_name.size())) {
            return std::nullopt;
        }
        std::size_t length = 0;
        const lxb_char_t *data = lxb_dom_element_get_attribute(element(n), bytes(attr_name), attr_name.size(), &length);
        return std::string(view(data, length));
    }

    void set_attribute(node *n, std::string_view attr_name, std::string_view value) {
        lxb_dom_element_set_attribute(element(n), bytes(attr_name), attr_name.size(), bytes(value), value.size());
    }

    void remove_attribute(node *n, std::string_view attr_name) {
        lxb_dom_element_remove_attribute(element(n), bytes(attr_name), attr_name.size());
    }

    std::string text(node *n) {
        if (n->type == LXB_DOM_NODE_TYPE_TEXT || n->type == LXB_DOM_NODE_TYPE_COMMENT) {
            const lexbor_str_t &data = lxb_dom_interface_character_data(n)->data;
            return std::string(view(data.data, data.length));
        }
        std::size_t length = 0;
        lxb_char_t *content = lxb_dom_node_text_content(n, &length);
        std::string out(view(content, length));
        if (content != nullptr) {
            lxb_dom_document_destroy_text(n->owner_document, content);
        }
        return out;
    }

    node *parent(const node *n) { return n->parent; }
    node *first_child(const node *n) { return n->first_child; }
    node *next(const node *n) { return n->next; }

    std::vector<node *> children(const node *n) {
        std::vector<node *> list;
        for (node *child = n->first_child; child != nullptr; child = child->next) {
            list.push_back(child);
        }
        return list;
    }

    std::vector<node *> element_children(const node *n) {
        std::vector<node *> list;
        for (node *child = n->first_child; child != nullptr; child = child->next) {
            if (is_element(child)) {
                list.push_back(child);
            }
        }
        return list;
    }

    std::size_t depth(const node *n) {
        std::size_t count = 0;
        for (const node *up = n->parent; up != nullptr; up = up->parent) {
            ++count;
        }
        return count;
    }

    // Without lexbor's mutation events: nodes from parse() belong to the fragment's own document, which has none
    void insert_before(node *reference, node *n) {
        lxb_dom_node_remove_wo_events(n);
        lxb_dom_node_insert_before_wo_events(reference, n);
    }

    void insert_after(node *reference, node *n) {
        lxb_dom_node_remove_wo_events(n);
        lxb_dom_node_insert_after_wo_events(reference, n);
    }

    void append(node *parent_node, node *n) {
        lxb_dom_node_remove_wo_events(n);
        lxb_dom_node_insert_child_wo_events(parent_node, n);
    }

    void prepend(node *parent_node, node *n) {
        if (parent_node->first_child != nullptr) {
            insert_before(parent_node->first_child, n);
        } else {
            append(parent_node, n);
        }
    }

    void remove(node *n) {
        lxb_dom_node_remove_wo_events(n);
        lxb_dom_node_destroy_deep(n);
    }
}
