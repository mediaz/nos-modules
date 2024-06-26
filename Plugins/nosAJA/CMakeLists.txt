# Copyright MediaZ Teknoloji A.S. All Rights Reserved.

# Dependencies
# ------------

# TODO: Make External/ajalibraries a submodule (https://github.com/aja-video/ntv2)?
#list(APPEND CMAKE_MODULE_PATH  ${CMAKE_CURRENT_LIST_DIR}/External/libajantv2/cmake)

# Caution: AJA wants C++11 standard. Change it back to our's after processing AJA
set(CMAKE_CXX_STANDARD 11)

set(AJANTV2_DISABLE_DEMOS ON)
set(AJANTV2_DISABLE_DRIVER ON)
set(AJANTV2_DISABLE_TESTS ON)
set(AJANTV2_DISABLE_TOOLS ON)
set(AJANTV2_DISABLE_PLUGINS ON)


#if(WIN32)
	remove_definitions(-DNOMINMAX)
#endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/External/libajantv2 "${CMAKE_CURRENT_BINARY_DIR}/ajalibraries")

get_targets(NOSAJA_EXTERNAL_TARGETS "./External/libajantv2")
group_targets("${NOSAJA_EXTERNAL_TARGETS}" "External")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_DEBUG_POSTFIX "")

nos_generate_flatbuffers("${CMAKE_CURRENT_SOURCE_DIR}/Config" "${CMAKE_CURRENT_SOURCE_DIR}/Source" "cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../nosUtilities;${CMAKE_CURRENT_SOURCE_DIR}/../nosMediaIO;${NOS_SDK_DIR}/types" nosAJA_generated)

list(APPEND DEPENDENCIES stb ajantv2 ${NOS_PLUGIN_SDK_TARGET} ${NOS_SYS_VULKAN_TARGET} nosAJA_generated)
list(APPEND INCLUDE_FOLDERS
    ${EXTERNAL_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../nosMediaIO/Source
    ${CMAKE_CURRENT_LIST_DIR}/External
    ${CMAKE_CURRENT_LIST_DIR}/External/libajantv2
#   ${CMAKE_CURRENT_LIST_DIR}/External/libajantv2/ajantv2/src/win
    ${CMAKE_CURRENT_LIST_DIR}/External/libajantv2/ajantv2/includes)

nos_add_plugin("nosAJA" "${DEPENDENCIES}" "${INCLUDE_FOLDERS}")
