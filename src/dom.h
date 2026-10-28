#pragma once
// The HTML document bootstrap-email works on: lexbor's DOM, with the few operations the converters and the inliner
// need, and written out the way Nokogiri (libxml2) writes HTML, which is what the Ruby gem's output is made from.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct lxb_dom_node;
struct lxb_html_document;

namespace sc::bootstrap_email::dom {
    using node = lxb_dom_node;

    class document {
    public:
        explicit document(std::string_view html);
        ~document();
        document(const document &) = delete;
        document &operator=(const document &) = delete;

        node *html() const;
        node *head() const;
        node *body() const;

        // The elements a CSS selector list matches, in document order; none when lexbor can't read the selector
        std::vector<node *> select(std::string_view selectors) const;
        node *first(std::string_view selectors) const;
        // Whether an element matches a selector list
        bool matches(node *element, std::string_view selectors) const;
        // The elements of a list that match a selector list
        std::vector<node *> filter(const std::vector<node *> &elements, std::string_view selectors) const;

        // HTML parsed as the children of a <context> element would be, not yet in the document
        std::vector<node *> parse(std::string_view html, std::string_view context = "div");
        node *create_text(std::string_view text);
        node *create_element(std::string_view name);

        // The document as Nokogiri's to_html(encoding: 'US-ASCII') writes it: characters outside ASCII as
        // character references, and libxml2's line breaks between block elements
        std::string to_html() const;

    private:
        lxb_html_document *doc;
        struct selector_engine;
        selector_engine *engine;
    };

    bool is_element(const node *n);
    bool is_text(const node *n);
    bool is_comment(const node *n);
    // An element's name, lower case; "" for other nodes
    std::string name(const node *n);
    std::optional<std::string> attribute(node *element, std::string_view name);
    void set_attribute(node *element, std::string_view name, std::string_view value);
    void remove_attribute(node *element, std::string_view name);
    // Text and comment nodes' text; an element's text content
    std::string text(node *n);

    node *parent(const node *n);
    node *first_child(const node *n);
    node *next(const node *n);
    std::vector<node *> children(const node *n);
    std::vector<node *> element_children(const node *n);
    std::size_t depth(const node *n);

    // Moves a node (out of wherever it is) to before, after or into the end of another
    void insert_before(node *reference, node *n);
    void insert_after(node *reference, node *n);
    void append(node *parent, node *n);
    void prepend(node *parent, node *n);
    // Takes a node out of the document (and frees it with its children)
    void remove(node *n);
}
