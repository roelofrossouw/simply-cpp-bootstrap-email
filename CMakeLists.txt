cmake_minimum_required(VERSION 3.22)
set(SC_MODULE bootstrap-email)
project(simply-cpp-${SC_MODULE} HOMEPAGE_URL "https://github.com/roelofrossouw/simply-cpp-bootstrap-email" LANGUAGES C CXX)
include(cmake/sc_bootstrap.cmake)

option(SC_BOOTSTRAP_EMAIL_BUILD_TESTS "Build simply-cpp-bootstrap-email tests" ${PROJECT_IS_TOP_LEVEL})

get_sc_version()
message(STATUS "SimplyCPP ${SC_MODULE} Version: ${SC_VERSION}")

# sc::params for the bootstrap-email program.
find_or_install_package(sc-core simply-cpp-dev simply-cpp 1.26.0)

set(SOURCE_OBJECTS)
set(SOURCE_LIBRARIES)
set(SOURCE_LINK_LIBRARIES)

# lexbor (Apache-2.0), the HTML parser, DOM and CSS selectors this module works on: the modules it needs from
# lexbor/lexbor v3.0.0 (third_party/lexbor/version), vendored in third_party/lexbor, are compiled into this module's
# libraries; their headers stay out of the installed ones. Hidden, so a program that also links a system lexbor gets
# its own.
set(LEXBOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lexbor/source)
set(LEXBOR_SOURCES)
foreach (part core dom html css selectors tag ns)
    file(GLOB_RECURSE part_sources CONFIGURE_DEPENDS ${LEXBOR_DIR}/lexbor/${part}/*.c)
    list(APPEND LEXBOR_SOURCES ${part_sources})
endforeach ()
file(GLOB LEXBOR_PORT_SOURCES CONFIGURE_DEPENDS ${LEXBOR_DIR}/lexbor/ports/posix/lexbor/core/*.c)
add_library(sc-bootstrap-email-lexbor OBJECT ${LEXBOR_SOURCES} ${LEXBOR_PORT_SOURCES})
set_target_properties(sc-bootstrap-email-lexbor PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99 EXCLUDE_FROM_ALL ON
        C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(sc-bootstrap-email-lexbor PRIVATE ${LEXBOR_DIR})
target_compile_definitions(sc-bootstrap-email-lexbor PRIVATE LEXBOR_STATIC)
target_compile_options(sc-bootstrap-email-lexbor PRIVATE -w) # upstream's code, upstream's warnings
list(APPEND SOURCE_OBJECTS $<TARGET_OBJECTS:sc-bootstrap-email-lexbor>)

# bootstrap-email's stylesheets (compiled from its SCSS with its default settings, see scripts/update-bootstrap-email.sh),
# templates and layout, built into the library
set(BOOTSTRAP_EMAIL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bootstrap-email)
file(GLOB BOOTSTRAP_EMAIL_RESOURCES CONFIGURE_DEPENDS ${BOOTSTRAP_EMAIL_DIR}/css/*.css ${BOOTSTRAP_EMAIL_DIR}/templates/*.html
        ${BOOTSTRAP_EMAIL_DIR}/layout.html ${BOOTSTRAP_EMAIL_DIR}/VERSION)
set(BOOTSTRAP_EMAIL_RESOURCES_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/resources.cpp)
add_custom_command(OUTPUT ${BOOTSTRAP_EMAIL_RESOURCES_CPP}
        COMMAND ${CMAKE_COMMAND} -DOUTPUT=${BOOTSTRAP_EMAIL_RESOURCES_CPP} -DBASE=${BOOTSTRAP_EMAIL_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resources.cmake
        DEPENDS ${BOOTSTRAP_EMAIL_RESOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resources.cmake
        COMMENT "Embedding bootstrap-email's stylesheets and templates")

add_sc_object(sc_bootstrap_email
        SOURCES src/css.cpp src/css.h src/dom.cpp src/dom.h src/inliner.cpp src/inliner.h
                src/converters.cpp src/converters.h src/beautify.cpp src/beautify.h src/resources.h
                ${BOOTSTRAP_EMAIL_RESOURCES_CPP}
        INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src ${LEXBOR_DIR}
        LINK_LIBRARIES sc::sc-core
        PUBLIC_LINK_LIBRARIES sc::sc-core)
target_compile_definitions(sc-obj-sc_bootstrap_email PRIVATE LEXBOR_STATIC)

add_sc_libraries(DESCRIPTION "simply-cpp bootstrap-email: Bootstrap HTML made into email-safe HTML, a C++ port of bootstrap-email")
target_compile_features(sc-bootstrap-email PUBLIC cxx_std_20)
target_compile_features(sc-bootstrap-email-shared PUBLIC cxx_std_20)

install_sc_module(PATH_VARS CMAKE_INSTALL_INCLUDEDIR)
install(FILES cmake/SimplyCppFunctions.cmake cmake/sc_bootstrap.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sc-bootstrap-email
        COMPONENT development)

# bootstrap-email, the program: a drop-in for the Ruby gem's command (a file, or standard input, to standard output)
add_executable(bootstrap-email src/main.cpp)
target_link_libraries(bootstrap-email PRIVATE sc::sc-bootstrap-email-shared)
target_compile_definitions(bootstrap-email PRIVATE SC_BOOTSTRAP_EMAIL_VERSION="${SC_VERSION}")
sc_program_info(bootstrap-email)
install(TARGETS bootstrap-email RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
sc_man_page(bootstrap-email EXTRA ${CMAKE_CURRENT_SOURCE_DIR}/man/bootstrap-email.md COMPONENT runtime)

if (SC_BOOTSTRAP_EMAIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests sc-bootstrap-email-test)
endif ()

package_sc_module(DEV_DEPENDS "simply-cpp-dev (>= 1.26.0~)")
