#pragma once
// bootstrap-email's converters (lib/bootstrap-email/converters): Bootstrap's classes made into the tables email clients
// lay out. Ruby builds each from a template string with the element's HTML in it and parses that; here the template is
// parsed on its own and the element's nodes are moved into it, which gives the same tree (an HTML5 parser would move a
// <div> out of a <tr>, libxml2's doesn't).

#include "dom.h"

namespace sc::bootstrap_email {
    // Compiler#compile_html: the converters before the CSS is inlined
    void convert(dom::document &doc);
    // Compiler#configure_html: the head's <style> for what can't be inlined, the <meta> tags emails need, the version
    void configure(dom::document &doc);
}
