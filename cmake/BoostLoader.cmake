set(BOOST_VERSION 1.87.0)

set(BOOST_COMPONENTS
        log
        container
        locale
        process
        # Align needed by boost::json
        align
        system
)
find_package(Boost ${BOOST_VERSION} EXACT COMPONENTS ${BOOST_COMPONENTS} QUIET)
if (NOT Boost_FOUND)
    # 检查是否有预下载的 Boost（在 builder 镜像中）
    set(PRE_DOWNLOADED_BOOST_DIR "/usr/local/src/boost-${BOOST_VERSION}")
    if (EXISTS "${PRE_DOWNLOADED_BOOST_DIR}/CMakeLists.txt")
        message(STATUS "Using pre-downloaded Boost from ${PRE_DOWNLOADED_BOOST_DIR}")
        set(BOOST_INCLUDE_LIBRARIES ${BOOST_COMPONENTS})
        set(BOOST_ENABLE_CMAKE ON)
        # 使用预下载的 Boost 目录
        FetchContent_Declare(
                Boost
                SOURCE_DIR "${PRE_DOWNLOADED_BOOST_DIR}"
        )
        FetchContent_MakeAvailable(Boost)
    else ()
        message(STATUS "Boost (or some required components) not found, falling back to FetchContent instead")

        set(BOOST_INCLUDE_LIBRARIES ${BOOST_COMPONENTS})
        set(BOOST_ENABLE_CMAKE ON)
        FetchContent_Declare(
                Boost
                URL "https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}-cmake.tar.xz"
        )
        FetchContent_MakeAvailable(Boost)
    endif ()

    set(Boost_FOUND TRUE)
    set(Boost_INCLUDE_DIRS "$<BUILD_INTERFACE:${Boost_SOURCE_DIR}/libs/headers/include>")
    set(Boost_LIBRARIES "")  # cmake-lint: disable=C0103
    foreach (component ${BOOST_COMPONENTS})
        list(APPEND Boost_LIBRARIES "Boost::${component}")
    endforeach ()
endif ()
include_directories(${Boost_INCLUDE_DIRS})