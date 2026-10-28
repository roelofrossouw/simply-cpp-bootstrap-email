#pragma once
// bootstrap-email's own files, built into the library (cmake/embed_resources.cmake)

#include <string_view>

namespace sc::bootstrap_email {
    // A file below third_party/bootstrap-email, e.g. "css/bootstrap-email.css" or "templates/table.html"
    std::string_view resource(std::string_view name);
}
