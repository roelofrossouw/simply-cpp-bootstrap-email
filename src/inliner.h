#pragma once
// A port of premailer (1.27, BSD-3-Clause, https://github.com/premailer/premailer), the parts bootstrap-email uses:
// its nokogiri_fast adapter's to_inline_css, with bootstrap-email's options (css_string, preserve_reset: false).

#include "css.h"
#include "dom.h"

#include <string_view>

namespace sc::bootstrap_email {
    class inliner {
    public:
        // Reads the stylesheet, then the document's <style> tags, and takes those (and stylesheet links) out of the
        // document, as Premailer.new does
        inliner(dom::document &doc, std::string_view stylesheet);

        // Puts the CSS into each element's style attribute, a few properties also into HTML attributes (bgcolor,
        // align, ...), and what can't be inlined (:hover, @media) into a <style> in the head
        void inline_css();

    private:
        dom::document &doc;
        std::string_view stylesheet;
        std::vector<css::rule> rules; // the page's own
    };
}
