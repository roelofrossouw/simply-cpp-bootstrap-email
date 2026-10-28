// Each fixture made into an email, against what the Ruby gem (bootstrap-email 1.8.1) makes of it, line by line.
// A difference shows the first line that differs; the whole output is left in <fixture>.actual.html in the build
// directory, for a diff.

#include "sc_test.h"

#include <sc_bootstrap_email.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
    std::string read(const std::filesystem::path &file) {
        std::ifstream in{file, std::ios::binary};
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    std::vector<std::string> lines(const std::string &text) {
        std::vector<std::string> list;
        std::stringstream in{text};
        for (std::string line; std::getline(in, line);) {
            list.push_back(line);
        }
        return list;
    }
}

int main() {
    const std::filesystem::path fixtures = SC_BOOTSTRAP_EMAIL_FIXTURES;
    for (const char *name : {"webframe", "components", "fragment", "styles", "utilities"}) {
        SECTION(name);
        const std::string actual = sc::bootstrap_email::compile(read(fixtures / (std::string(name) + ".html")));
        const std::string expected = read(fixtures / (std::string(name) + ".expected.html"));
        std::ofstream(std::string(name) + ".actual.html", std::ios::binary) << actual;
        const auto got = lines(actual);
        const auto want = lines(expected);
        std::size_t line = 0;
        while (line < got.size() && line < want.size() && got[line] == want[line]) {
            ++line;
        }
        const bool same = line == got.size() && line == want.size();
        CHECK_MSG(same, "line " + std::to_string(line + 1) + "\n  got:      " + (line < got.size() ? got[line].substr(0, 400) : "(end)") +
                               "\n  expected: " + (line < want.size() ? want[line].substr(0, 400) : "(end)"));
    }
    sc::bootstrap_email::compile("<p>Just a paragraph</p>"); // a fragment without html or body
    CHECK_EQ(sc::bootstrap_email::upstream_version(), std::string("1.8.1"));
    TEST_SUMMARY();
}
