set(JSON_VERSION 3.11.3)

if (NOT DEFINED CMAKE_BUILD_EARLY_EXPANSION)
    include(FetchContent)

    FetchContent_Declare(
        nlohmann-json
        URL "https://github.com/nlohmann/json/releases/download/v${JSON_VERSION}/json.hpp"
        URL_HASH SHA256=9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6
        DOWNLOAD_NO_EXTRACT YES
        DOWNLOAD_NAME json_impl.hpp
        DOWNLOAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann"
    )

    FetchContent_MakeAvailable(nlohmann-json)
endif ()

if (DEFINED ENV{IDF_PATH})
    idf_component_register(INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
else ()
    add_library(nlohmann-json INTERFACE)
    target_include_directories(nlohmann-json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
endif ()
