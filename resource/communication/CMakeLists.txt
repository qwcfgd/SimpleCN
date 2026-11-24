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
set_property(GLOBAL PROPERTY PEAK_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../dll")
function(peak_deploy target)
    get_property(sdk_root GLOBAL PROPERTY PEAK_SDK_ROOT)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(arch x64)
    else()
        set(arch x86)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sdk_root}/CAN/${arch}/PCANBasic.dll" "$<TARGET_FILE_DIR:${target}>/dll/PCANBasic.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sdk_root}/LIN/${arch}/PLinApi.dll" "$<TARGET_FILE_DIR:${target}>/dll/PLinApi.dll")
endfunction()
