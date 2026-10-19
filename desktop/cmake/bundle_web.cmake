# cmake -DDIR=<web dir> -DOUT=<header> -P bundle_web.cmake
# Inlines app.css and the scripts referenced by index.html into one page,
# then writes it as a C byte array (IndexHtml.hpp). The unbundled files
# keep working from disk (AMMO_WEB_DIR) for editing.
file(READ "${DIR}/index.html" PAGE)
foreach(css app.css)
  if(EXISTS "${DIR}/${css}")
    file(READ "${DIR}/${css}" CONTENT)
    string(REPLACE "<link rel=\"stylesheet\" href=\"${css}\">" "<style>\n${CONTENT}\n</style>" PAGE "${PAGE}")
  endif()
endforeach()
foreach(js knobs.js shells.js app.js)
  if(EXISTS "${DIR}/${js}")
    file(READ "${DIR}/${js}" CONTENT)
    string(REPLACE "<script src=\"${js}\"></script>" "<script>\n${CONTENT}\n</script>" PAGE "${PAGE}")
  endif()
endforeach()
file(WRITE "${OUT}.html" "${PAGE}")
file(READ "${OUT}.html" HEX_CONTENT HEX)
string(LENGTH "${HEX_CONTENT}" HEX_LEN)
math(EXPR LEN "${HEX_LEN} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," ARRAY "${HEX_CONTENT}")
file(WRITE "${OUT}"
"// Generated from ${DIR} by cmake/bundle_web.cmake. Do not edit.
#pragma once
#include <cstddef>
static const unsigned char kIndexHtml[] = {${ARRAY}0x00};
static const size_t kIndexHtmlLen = ${LEN};
")
