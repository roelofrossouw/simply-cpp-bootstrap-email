#pragma once
// A port of htmlbeautifier (1.4, MIT, https://github.com/threedaymonkey/htmlbeautifier): bootstrap-email's last step,
// which indents the HTML two spaces a level

#include <string>
#include <string_view>

namespace sc::bootstrap_email {
    std::string beautify(std::string_view html);
}
