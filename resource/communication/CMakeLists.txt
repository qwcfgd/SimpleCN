cmake_minimum_required(VERSION 3.16)
add_library(peak_communication STATIC
    ../driverCan/pcanBasicClass.cpp ../driverCan/tstPeakCan.cpp
    ../driverLin/plinApiClass.cpp ../driverLin/tstPeakLin.cpp
    ChannelTypes.h PeakLibrary.h HardwareBackend.h HardwareBackend.cpp
    SoftwareChannel.h SoftwareChannel.cpp SignalLinExtensions.cpp)
set_target_properties(peak_communication PROPERTIES AUTOMOC ON)
target_compile_features(peak_communication PUBLIC cxx_std_11)
target_compile_definitions(peak_communication PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(peak_communication PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(peak_communication PUBLIC Qt${QT_VERSION_MAJOR}::Core)
if(MINGW)
    target_compile_options(peak_communication PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()
# Retain the helper name for communication-only probes and tests.
function(peak_deploy target)
    driver_deploy(${target})
endfunction()
