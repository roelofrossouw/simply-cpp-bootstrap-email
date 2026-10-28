# simply-cpp-bootstrap-email

`sc-bootstrap-email` makes HTML written with [Bootstrap 5](https://getbootstrap.com)'s classes into HTML that email
clients (Outlook, Gmail, Apple Mail) show as intended: the layout as tables, the CSS in each element's `style`
attribute (and the `bgcolor`, `align` and `valign` attributes old clients read), and a head stylesheet for what can't
be inlined. It's a C++ port of [bootstrap-email](https://bootstrapemail.com) 1.8.1, and gives what its Ruby gem's
`bootstrap-email` command gives, byte for byte, without Ruby: 4 to 12 ms an email, after about 0.1 s for the first one
in a process (reading the stylesheet).

**Documentation:** the [simply-cpp wiki](https://github.com/roelofrossouw/simply-cpp/wiki) has the [sc-bootstrap-email reference](https://github.com/roelofrossouw/simply-cpp/wiki/Reference-sc-bootstrap-email).

```cpp
#include <sc_bootstrap_email.h>

const std::string email = sc::bootstrap_email::compile(R"(
    <body class="bg-light">
      <div class="container">
        <div class="card my-4"><div class="card-body">
          <h1 class="h3">Welcome</h1>
          <p>Your account is ready.</p>
          <a class="btn btn-primary" href="https://example.com">Sign in</a>
        </div></div>
      </div>
    </body>)");
```

A whole page keeps its head; a `<body>` alone (no `<head>`) gets bootstrap-email's layout with the `<meta>` tags emails
need. The page's own `<style>` tags are inlined too, and taken out; rules that can't be inlined (`:hover`, `@media`
queries) stay in a `<style>` in the head. A `<style data-premailer="ignore">` is left as it is.

## The program

`bootstrap-email` reads a file, `--string`, or standard input, and writes the email to standard output (or `-o FILE`),
like the gem's command, so it can stand in for it:

```bash
bootstrap-email welcome.html > welcome-email.html
```

PHP sites use it through [sc-phplib](https://github.com/roelofrossouw/simply-cpp-phplib), in the PHP process:
`SC\BootstrapEmail::Format($Html)`.

## What it supports

Everything bootstrap-email 1.8.1 does, with its default settings: its stylesheet (buttons, badges, alerts, cards,
grid, stacks, spacing, colours, typography, borders, ...), all 27 converters (`.btn`, `.card`, `.row`/`.col-*`,
`.container`, `.stack-row`/`.stack-col`, `.space-y-*`, `my-*`/`mt-*`/`mb-*`, `p-*`, `s-*`, `ax-*`, `bg-*` on a `div`,
`<hr>`, `<block>` and `.to-table`, `<preview>`), and premailer's inlining. There is no configuration file: a
`bootstrap-email.config.rb`'s SCSS variables aren't read.

How it's made, and how it stays the same as the gem:

- [lexbor](https://github.com/lexbor/lexbor) 3.0.0 (Apache-2.0) parses the HTML and matches the CSS selectors; the
  parts it needs are compiled in, from `third_party/lexbor`.
- The stylesheets, templates and layout are bootstrap-email's, in `third_party/bootstrap-email`, the SCSS compiled with
  its default settings by `scripts/update-bootstrap-email.sh`.
- The compiler, premailer's inlining and css_parser's cascade and shorthands, and htmlbeautifier are ported to C++,
  each in its own file under `src/`. Where an HTML5 parser builds a different tree from libxml2's (a `<div>` in a
  `<tr>`, a table's implied `<tbody>`), the port builds libxml2's, and it writes the HTML the way Nokogiri does.
- `tests/fixtures` holds pages and what the gem makes of them; the tests compare the output line by line.

## Building

```bash
cmake -B cmake-build-local -S .
cmake --build cmake-build-local
ctest --test-dir cmake-build-local
```

It needs sc-core (for the program's `sc::params`) and a C++20 compiler.

## License

Apache License 2.0, see [LICENSE](LICENSE). bootstrap-email (MIT), premailer (BSD-3-Clause), css_parser (MIT),
htmlbeautifier (MIT) and lexbor (Apache-2.0): see [NOTICE](NOTICE) and `third_party`.
