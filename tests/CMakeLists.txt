set(SC_TEST_LINK_LIBRARIES sc-bootstrap-email)

# The Ruby gem's output for each fixture (tests/fixtures/*.expected.html, made with bootstrap-email 1.8.1)
add_sc_test(fixtures LABELS unit)
target_compile_definitions(test-fixtures PRIVATE SC_BOOTSTRAP_EMAIL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
