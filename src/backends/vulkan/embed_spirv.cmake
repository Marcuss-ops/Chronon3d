if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "INPUT, OUTPUT and SYMBOL are required")
endif()
file(READ "${INPUT}" SPIRV HEX)
string(LENGTH "${SPIRV}" SPIRV_LENGTH)
math(EXPR BYTE_COUNT "${SPIRV_LENGTH} / 2")
set(BYTES "")
foreach(INDEX RANGE 0 ${BYTE_COUNT})
    math(EXPR OFFSET "${INDEX} * 2")
    if(OFFSET LESS SPIRV_LENGTH)
        string(SUBSTRING "${SPIRV}" ${OFFSET} 2 BYTE)
        string(APPEND BYTES "0x${BYTE}, ")
    endif()
endforeach()
file(WRITE "${OUTPUT}"
    "#pragma once\n#include <cstddef>\n#include <cstdint>\n"
    "namespace chronon3d::backends::vulkan::shaders {\n"
    "alignas(4) inline constexpr std::uint8_t ${SYMBOL}[] = {${BYTES}};\n"
    "inline constexpr std::size_t ${SYMBOL}_size = sizeof(${SYMBOL});\n}\n")
