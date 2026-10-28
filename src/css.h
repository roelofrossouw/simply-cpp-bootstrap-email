#pragma once
// CSS the way premailer's css_parser gem (1.22) reads, folds and writes it, which decides what ends up in each style
// attribute: declarations in their order, !important, the cascade by specificity, and shorthands taken apart and put
// back together.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sc::bootstrap_email::css {
    // A declaration's value, without its !important
    struct value {
        std::string text;
        bool important = false;

        // Parsed as css_parser does: a trailing ';' and the first !important taken off, then trimmed.
        // Empty when nothing is left.
        static std::optional<value> parse(std::string_view raw);
        std::string to_string() const { return important ? text + " !important" : text; }
        bool operator==(const value &) const = default;
    };

    // A rule's declarations, in the order they were first set (CssParser::RuleSet::Declarations)
    class declarations {
    public:
        // Sets a property (lower-cased, trimmed). An !important value is only replaced by another !important one;
        // an empty value removes the property.
        void set(std::string_view property, std::string_view raw);
        void set(std::string_view property, const value &v);
        const value *get(std::string_view property) const;
        value *get(std::string_view property);
        bool has(std::string_view property) const { return get(property) != nullptr; }
        void erase(std::string_view property);
        std::size_t size() const { return items.size(); }
        bool empty() const { return items.empty(); }
        const std::vector<std::pair<std::string, value>> &list() const { return items; }

        // Puts the replacements where the property was (absent ones, std::nullopt, are skipped), as css_parser's
        // replace_declaration! does
        void replace(std::string_view property, const std::vector<std::pair<std::string, std::optional<std::string>>> &replacements,
                     bool preserve_importance);

        // "a: b; c: d !important;"
        std::string to_string() const;

        void expand_shorthands();
        void create_shorthands();

    private:
        std::vector<std::pair<std::string, value>> items;

        void expand_border();
        void expand_dimensions();
        void expand_font();
        void expand_background();
        void expand_list_style();
        void create_background();
        void create_dimensions();
        void create_border();
        void create_font();
        void create_from(const std::vector<std::string> &properties, std::string_view shorthand);
    };

    // A declaration block, e.g. a style attribute or what's between a rule's braces
    declarations parse_declarations(std::string_view block);

    // css_parser's specificity of one selector: the decimal digits of (0, ids, classes etc., elements) read as a number
    int specificity(std::string_view selector);

    // The cascade of the rule sets that apply to one element, in the order they apply (each with its specificity),
    // into one, as CssParser.merge does
    declarations merge(std::vector<std::pair<int, declarations>> rule_sets);

    struct rule {
        std::vector<std::string> selectors;
        declarations block;
        std::vector<std::string> media; // "all", or the media queries the rule is inside
    };

    // A stylesheet's rules, in order: comments dropped, @media blocks' rules with their queries, @import and @charset
    // ignored
    std::vector<rule> parse_stylesheet(std::string_view css);

    // A selector the way premailer matches it: ":link" taken off, element names in lower case
    std::string premailer_selector(std::string_view selector);
    // Rules premailer can't put into style attributes (:hover and the like, @media queries), kept in a <style>
    bool unmergeable(std::string_view selector, const std::vector<std::string> &media);
    // Rules written back as a stylesheet, as css_parser's Parser#to_s does
    std::string to_stylesheet(const std::vector<rule> &rules);
}
