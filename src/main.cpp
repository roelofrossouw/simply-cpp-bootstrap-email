// bootstrap-email, the program: an email's Bootstrap HTML, from a file, --string or standard input, as email HTML on
// standard output, as the Ruby gem's command gives it.

#include <sc_bootstrap_email.h>

#include <params.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

int main(int argc, char **argv) {
    const sc::params args{argc, argv, {
        {"name", "bootstrap-email"},
        {"description", "Makes HTML written with Bootstrap 5's classes into HTML email clients show as intended: the layout as "
                        "tables, the CSS in each element's style attribute. A C++ port of bootstrap-email " +
                        sc::bootstrap_email::upstream_version() + " (bootstrapemail.com) that gives what its Ruby gem's command "
                        "gives."},
        {"version", SC_BOOTSTRAP_EMAIL_VERSION},
        {"epilog", "Without FILE or --string it reads standard input. A page without a <head> gets bootstrap-email's layout."},
        {"options", {
            {"string", {{"short", "s"}, {"value", "HTML"}, {"help", "make this HTML into an email, instead of a file"}}},
            {"output", {{"short", "o"}, {"value", "FILE"}, {"help", "write to FILE instead of standard output"}}}}},
        {"positional", {
            {"file", {{"required", false}, {"help", "the HTML file (- for standard input)"}}}}}}};
    try {
        std::string html;
        if (const auto *text = args.lookup("string")) {
            html = text->get<std::string>();
        } else if (const auto *file = args.lookup("file"); file != nullptr && file->get<std::string>() != "-") {
            std::ifstream in{file->get<std::string>(), std::ios::binary};
            if (!in) {
                std::cerr << "bootstrap-email: can't read " << file->get<std::string>() << '\n';
                return 1;
            }
            std::stringstream read;
            read << in.rdbuf();
            html = read.str();
        } else {
            html.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
        }
        const std::string email = sc::bootstrap_email::compile(html);
        if (const auto *output = args.lookup("output")) {
            std::ofstream out{output->get<std::string>(), std::ios::binary};
            out << email;
            if (!out) {
                std::cerr << "bootstrap-email: can't write " << output->get<std::string>() << '\n';
                return 1;
            }
        } else {
            std::cout << email;
        }
    } catch (const std::exception &e) {
        std::cerr << "bootstrap-email: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
