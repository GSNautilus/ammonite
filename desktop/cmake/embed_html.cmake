# cmake -DIN=<index.html> -DOUT=<header> -P embed_html.cmake
# Writes the file as a C byte array (no string literal size limits).
file(READ "${IN}" HEX_CONTENT HEX)
string(LENGTH "${HEX_CONTENT}" HEX_LEN)
math(EXPR LEN "${HEX_LEN} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," ARRAY "${HEX_CONTENT}")
file(WRITE "${OUT}"
"// Generated from test/web/index.html by cmake/embed_html.cmake. Do not edit.
#pragma once
#include <cstddef>
static const unsigned char kIndexHtml[] = {${ARRAY}0x00};
static const size_t kIndexHtmlLen = ${LEN};
")
