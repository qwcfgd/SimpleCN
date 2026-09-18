# Fixed native dependencies. Override FETCHCONTENT_SOURCE_DIR_* for offline builds.
include(FetchContent)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_Declare(signal_dbcppp
    GIT_REPOSITORY https://github.com/xR3b0rn/dbcppp.git
    GIT_TAG b520607559223ac02a7ca87d47b4932cd9f3d21b)
FetchContent_Declare(signal_multiprecision
    GIT_REPOSITORY https://github.com/boostorg/multiprecision.git
    GIT_TAG de3aded8632e0ef0f17dcaf274f5699a25139738)
FetchContent_Declare(signal_boost_math
    GIT_REPOSITORY https://github.com/boostorg/math.git
    GIT_TAG 44af29a78c85ee89ce37f7f43d532afd05c3d981)
foreach(dependency signal_dbcppp signal_multiprecision signal_boost_math)
    FetchContent_GetProperties(${dependency})
    if(NOT ${dependency}_POPULATED)
        FetchContent_Populate(${dependency})
    endif()
endforeach()
# Build only the DBC library, without KCD, CLI tools or upstream tests.
file(GLOB dbcppp_sources CONFIGURE_DEPENDS "${signal_dbcppp_SOURCE_DIR}/src/*.cpp")
set(dbcppp_include "${signal_dbcppp_SOURCE_DIR}/include")
if(MINGW AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    # GCC 8.1's Windows <filesystem> does not compile. The application reads
    # Unicode paths through QFile and uses LoadDBCFromIs only. Omit the unused
    # filesystem convenience API in a generated build copy, leaving the pinned
    # upstream checkout and the actual DBC parser unchanged.
    set(dbcppp_compat "${CMAKE_CURRENT_BINARY_DIR}/dbcppp-stream-only")
    file(COPY "${dbcppp_include}/" DESTINATION "${dbcppp_compat}/include")
    file(READ "${dbcppp_include}/dbcppp/Network.h" network_header)
    string(REPLACE "#include <filesystem>" "" network_header "${network_header}")
    string(REPLACE "        static std::map<std::string, std::unique_ptr<INetwork>> LoadNetworkFromFile(const std::filesystem::path& filename);" "" network_header "${network_header}")
    file(WRITE "${dbcppp_compat}/include/dbcppp/Network.h" "${network_header}")
    file(READ "${signal_dbcppp_SOURCE_DIR}/src/NetworkImpl.cpp" network_source)
    string(FIND "${network_source}" "std::map<std::string, std::unique_ptr<INetwork>> INetwork::LoadNetworkFromFile" convenience_start)
    if(convenience_start LESS 0)
        message(FATAL_ERROR "Pinned dbcppp layout changed; review the GCC 8 compatibility adapter")
    endif()
    string(SUBSTRING "${network_source}" 0 ${convenience_start} network_source)
    file(WRITE "${dbcppp_compat}/NetworkImpl.cpp" "${network_source}")
    list(REMOVE_ITEM dbcppp_sources "${signal_dbcppp_SOURCE_DIR}/src/NetworkImpl.cpp")
    list(APPEND dbcppp_sources "${dbcppp_compat}/NetworkImpl.cpp")
    set(dbcppp_include "${dbcppp_compat}/include")
endif()
add_library(signal_dbcppp SHARED ${dbcppp_sources})
set_target_properties(signal_dbcppp PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_definitions(signal_dbcppp PRIVATE DBCPPP_EXPORT)
target_include_directories(signal_dbcppp PUBLIC "${dbcppp_include}" PRIVATE "${signal_dbcppp_SOURCE_DIR}/src")
if(MINGW)
    target_include_directories(signal_dbcppp PRIVATE "${CMAKE_CURRENT_LIST_DIR}/compat")
endif()
target_include_directories(signal_dbcppp SYSTEM PRIVATE "${signal_dbcppp_SOURCE_DIR}/third-party/boost")
add_library(signal_exact_decimal INTERFACE)
target_include_directories(signal_exact_decimal SYSTEM INTERFACE
    "${signal_multiprecision_SOURCE_DIR}/include"
    "${signal_boost_math_SOURCE_DIR}/include"
    "${signal_dbcppp_SOURCE_DIR}/third-party/boost")
function(signal_deploy target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:signal_dbcppp>" "$<TARGET_FILE_DIR:${target}>")
endfunction()
