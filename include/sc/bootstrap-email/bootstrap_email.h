#pragma once
// HTML written with Bootstrap 5's classes made into HTML email clients show as intended: layout as tables, CSS in each
// element's style attribute (and the attributes old clients read), a head stylesheet for what can't be inlined.
// A C++ port of bootstrap-email (https://bootstrapemail.com, MIT), giving what its Ruby gem's `bootstrap-email`
// command gives, without Ruby.

#include <string>
#include <string_view>

namespace sc::bootstrap_email {
    // The email's HTML: a whole page, or a <body> alone, which gets bootstrap-email's layout (head and meta tags).
    // Throws std::runtime_error when the HTML can't be read at all (lexbor reads anything a browser would).
    std::string compile(std::string_view html);

    // The bootstrap-email version this follows, e.g. "1.8.1"
    std::string upstream_version();
}
