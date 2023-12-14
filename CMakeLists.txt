cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0042 NEW)

project(zt-node)

find_package(Threads)


# detect Windows
if(WIN32)
    set(BUILD_WIN32 TRUE)
endif()
if("${CMAKE_GENERATOR}" MATCHES "(Win64|IA64)")
    set(BUILD_WIN64 TRUE)
endif()
if(BUILD_WIN32
     OR BUILD_WIN64
     OR MSVC)
    set(BUILD_WIN TRUE)
endif()

# ------------------------------------------------------------------------------
# |                             SOURCE DIRECTORIES                             |
# ------------------------------------------------------------------------------


set(PROJ_DIR ${PROJECT_SOURCE_DIR})

set(LIBZT_DIR "${PROJ_DIR}/ext/libzt")
set(LWIP_SRC_DIR "${LIBZT_DIR}/ext/lwip/src")
set(ZTO_SRC_DIR "${LIBZT_DIR}/ext/ZeroTierOne")
set(LIBZT_SRC_DIR "${LIBZT_DIR}/src")


if(UNIX)
    set(LWIP_PORT_DIR ${LIBZT_DIR}/ext/lwip-contrib/ports/unix/port)
endif()
if(BUILD_WIN)
    set(LWIP_PORT_DIR ${LIBZT_DIR}/ext/lwip-contrib/ports/win32)
endif()

# ------------------------------------------------------------------------------
# |                             INCLUDE DIRECTORIES                            |
# ------------------------------------------------------------------------------
# ZeroTier
include_directories(${ZTO_SRC_DIR})
include_directories(${ZTO_SRC_DIR}/include)
include_directories(${ZTO_SRC_DIR}/node)
include_directories(${ZTO_SRC_DIR}/osdep)
# ZeroTier (ext)
#include_directories(${ZTO_SRC_DIR}/ext/miniupnpc)
include_directories(${ZTO_SRC_DIR}/ext/libnatpmp)
include_directories(${ZTO_SRC_DIR}/ext/prometheus-cpp-lite-1.0/core/include)
include_directories(${ZTO_SRC_DIR}/ext/prometheus-cpp-lite-1.0/simpleapi/include)
# libzt
include_directories(${LIBZT_DIR}/src)
include_directories(${LIBZT_DIR}/include)
# libzt (ext)
include_directories(${LIBZT_DIR}/ext/concurrentqueue)
include_directories(${LWIP_SRC_DIR}/include)
include_directories(${LWIP_PORT_DIR}/include)


# ------------------------------------------------------------------------------
# |                           DISABLE CENTRAL API                              |
# ------------------------------------------------------------------------------

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DZTS_DISABLE_CENTRAL_API=1")


# ------------------------------------------------------------------------------
# |                                BUILD TYPES                                 |
# ------------------------------------------------------------------------------

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "debug")
    set(BUILD_DEBUG ON)
    set(CMAKE_VERBOSE_MAKEFILE ON)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "release")
    set(BUILD_RELEASE ON)
    set(CMAKE_VERBOSE_MAKEFILE OFF)
    set(RELEASE_OPTIMIZATION "-O3")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -w")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -w")
    if(NOT MSVC)
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wno-everything")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wno-everything")
    endif()
endif()

# ------------------------------------------------------------------------------
# |                                    FLAGS                                   |
# ------------------------------------------------------------------------------

set(ZT_FLAGS "${ZT_FLAGS} -DZT_SDK=1")
set(ZT_FLAGS "${ZT_FLAGS} -DZT_USE_MINIUPNPC=1")
set(ZT_FLAGS "${ZT_FLAGS} -D_USING_LWIP_DEFINITIONS_=0")

set(ZT_FLAGS "${ZT_FLAGS} -DOMIT_JSON_SUPPORT=1")
if (NOT BUILD_WIN)
    set(ZT_FLAGS "${ZT_FLAGS} -Wno-unused-parameter")
    set(LWIP_FLAGS "${LWIP_FLAGS} -Wno-tautological-constant-out-of-range-compare -Wno-parentheses-equality")
endif()

if(BUILD_DEBUG)
    set(LWIP_FLAGS "${LWIP_FLAGS} -DLWIP_DBG_TYPES_ON=128")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DSOCKETS_DEBUG=128")
    # set (LWIP_FLAGS "${LWIP_FLAGS} -DLWIP_STATS_LARGE=1") set (LWIP_FLAGS
    # "${LWIP_FLAGS} -DLWIP_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DLINK_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DETHARP_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DIPFRAG_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DIP_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DICMP_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DIGMP_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DUDP_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DTCP_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DSYS_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DIP6_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DICMP6_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DIP6_FRAG_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DMLD6_STATS=1")
    set(LWIP_FLAGS "${LWIP_FLAGS} -DND6_STATS=1")
else()
    set(LWIP_FLAGS "${LWIP_FLAGS} -DLWIP_DBG_TYPES_ON=0")
endif()

if(BUILD_WIN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc -DNOMINMAX")
else()
    option(STACK_PROTECTOR "Compile with -fstack-protector" ON)
    if(STACK_PROTECTOR)
      set(STACK_PROTECTOR_FLAGS -fstack-protector)
    endif()

    set(CMAKE_C_FLAGS
            "${CMAKE_C_FLAGS} \
         ${ZT_FLAGS} \
         ${STACK_PROTECTOR_FLAGS}")

    set(CMAKE_C_FLAGS_DEBUG
            "${CMAKE_C_FLAGS_DEBUG} \
         -DLWIP_DEBUG=1 -DLIBZT_DEBUG=1")

    set(CMAKE_C_FLAGS_RELEASE
            "${CMAKE_C_FLAGS_RELEASE} \
         ${RELEASE_OPTIMIZATION} \
         ${STACK_PROTECTOR_FLAGS}")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
         ${ZT_FLAGS} -Wall -Wextra -std=c++11")

    set(CMAKE_CXX_FLAGS_DEBUG
            "${CMAKE_CXX_FLAGS_DEBUG} \
         -DLWIP_DEBUG=1 -DLIBZT_DEBUG=1")

    set(CMAKE_CXX_FLAGS_RELEASE
            "${CMAKE_CXX_FLAGS_RELEASE} \
         ${RELEASE_OPTIMIZATION}")
endif()

# WINDOWS-specific MSVC flags and libraries
if(BUILD_WIN)
    # 32-bit
    if(NOT BUILD_WIN64)
        set(WINLIBDIR,
                "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.16299.0/um/x86")
    endif()
    # 64-bit
    if(BUILD_WIN64)
        set(WINLIBDIR,
                "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.16299.0/um/x64")
    endif()
    # find_library (ws2_32_LIBRARY_PATH NAMES WS2_32 HINTS ${WINLIBDIR})
    # find_library (shlwapi_LIBRARY_PATH NAMES ShLwApi HINTS ${WINLIBDIR})
    set(ws2_32_LIBRARY_PATH "${WINLIBDIR}/WS2_32.Lib")
    set(shlwapi_LIBRARY_PATH "${WINLIBDIR}/ShLwApi.Lib")
    set(iphlpapi_LIBRARY_PATH "${WINLIBDIR}/iphlpapi.Lib")
    message(STATUS ${WINLIBDIR})
    message(STATUS "WS2_32=${ws2_32_LIBRARY_PATH}")
    message(STATUS "ShLwApi=${shlwapi_LIBRARY_PATH}")
    message(STATUS "liphlpapi=${iphlpapi_LIBRARY_PATH}")
    add_definitions(-DADD_EXPORTS=1)
endif()

# ------------------------------------------------------------------------------
# |                                   SOURCES                                  |
# ------------------------------------------------------------------------------

file(GLOB ztcoreSrcGlob ${ZTO_SRC_DIR}/node/*.cpp
         ${ZTO_SRC_DIR}/osdep/OSUtils.cpp ${ZTO_SRC_DIR}/osdep/PortMapper.cpp)

file(GLOB libnatpmpSrcGlob ${ZTO_SRC_DIR}/ext/libnatpmp/natpmp.c
    ${ZTO_SRC_DIR}/ext/libnatpmp/wingettimeofday.c
    ${ZTO_SRC_DIR}/ext/libnatpmp/getgateway.c)
if(NOT BUILD_WIN)
list(REMOVE_ITEM libnatpmpSrcGlob ${ZTO_SRC_DIR}/ext/libnatpmp/wingettimeofday.c)
endif()

file(
    GLOB
    libminiupnpcSrcGlob
    ${ZTO_SRC_DIR}/ext/miniupnpc/connecthostport.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/igd_desc_parse.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/minisoap.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/minissdpc.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/miniupnpc.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/miniwget.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/minixml.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/portlistingparse.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/receivedata.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/upnpcommands.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/upnpdev.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/upnperrors.c
    ${ZTO_SRC_DIR}/ext/miniupnpc/upnpreplyparse.c)

file(GLOB libztSrcGlob ${LIBZT_SRC_DIR}/*.cpp ${LANG_WRAPPER_FILE})

file(
    GLOB
    lwipSrcGlob
    ${LWIP_SRC_DIR}/netif/*.c
    ${LWIP_SRC_DIR}/api/*.c
    ${LWIP_PORT_DIR}/sys_arch.c
    ${LWIP_SRC_DIR}/core/*.c
    ${LWIP_SRC_DIR}/core/ipv4/*.c
    ${LWIP_SRC_DIR}/core/ipv6/*.c)
list(REMOVE_ITEM lwipSrcGlob ${LWIP_SRC_DIR}/netif/slipif.c)

# ------------------------------------------------------------------------------
# |                     OBJECT LIBRARIES (INTERMEDIATE)                        |
# ------------------------------------------------------------------------------

# zto_pic
add_library(zto_pic ${ztcoreSrcGlob})
set_target_properties(zto_pic PROPERTIES COMPILE_FLAGS "${ZT_FLAGS}"
    POSITION_INDEPENDENT_CODE ON)

# libnatpmp_pic
add_library(natpmp_pic ${libnatpmpSrcGlob})
set_target_properties(natpmp_pic PROPERTIES COMPILE_FLAGS "-DNATPMP_EXPORTS"
    POSITION_INDEPENDENT_CODE ON)

# miniupnpc_pic
add_library(miniupnpc_pic ${libminiupnpcSrcGlob})
target_compile_definitions(
    miniupnpc_pic
    PRIVATE MACOSX
                    ZT_USE_MINIUPNPC
                    MINIUPNP_STATICLIB
                    _DARWIN_C_SOURCE
                    MINIUPNPC_SET_SOCKET_TIMEOUT
                    MINIUPNPC_GET_SRC_ADDR
                    _BSD_SOURCE
                    _DEFAULT_SOURCE
                    MINIUPNPC_VERSION_STRING=\"2.0\"
                    UPNP_VERSION_STRING=\"UPnP/1.1\"
                    ENABLE_STRNATPMPERR
                    OS_STRING=\"Darwin/15.0.0\")
set_target_properties(miniupnpc_pic PROPERTIES POSITION_INDEPENDENT_CODE ON)

# lwip_pic
add_library(lwip_pic ${lwipSrcGlob})
set_target_properties(lwip_pic PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(lwip_pic PROPERTIES COMPILE_FLAGS "${LWIP_FLAGS}")

# libzt_pic
add_library(zt_pic ${libztSrcGlob})
set_target_properties(zt_pic PROPERTIES COMPILE_FLAGS "${ZT_FLAGS}"
    POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------------------
# |                                 NODEJS BINDING                             |
# ------------------------------------------------------------------------------

add_definitions(-DNAPI_VERSION=5)

include_directories(${CMAKE_JS_INC})

set(NODEJS_SRC_DIR "${PROJ_DIR}/src/native")
set(NODEJS_SRC_FILES 
    "${NODEJS_SRC_DIR}/binding.cc"       )

add_library(nodezt SHARED ${NODEJS_SRC_FILES} ${CMAKE_JS_SRC})
set_target_properties(nodezt PROPERTIES PREFIX "" SUFFIX ".node")

target_link_libraries(nodezt 
    ${CMAKE_JS_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    ${ws2_32_LIBRARY_PATH}
    ${shlwapi_LIBRARY_PATH}
    ${iphlpapi_LIBRARY_PATH}    
    zt_pic 
    lwip_pic 
    zto_pic
    natpmp_pic
    miniupnpc_pic)

if(MSVC AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
    # Generate node.lib
    execute_process(COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF} /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS})
endif()

# Include N-API wrappers
target_include_directories(nodezt PRIVATE "${PROJ_DIR}/node_modules/node-addon-api")
