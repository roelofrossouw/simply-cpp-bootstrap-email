# EXAMPLES
An email's HTML from a file:
```
bootstrap-email welcome.html > welcome-email.html
```
From a program, through standard input:
```
render-email | bootstrap-email -o email.html
```

# NOTES
The stylesheet, templates and layout are bootstrap-email's own, with its default settings; there is no configuration
file. PHP sites can use SC\BootstrapEmail::Format() from sc-phplib instead, without starting a program.

# SEE ALSO
https://bootstrapemail.com
