cmake_minimum_required(VERSION 3.12)

project(VulkanTest)

SET(CMAKE_CXX_STANDARD 23)

if(MSVC)
    # Warning C5246 is useless and complains about std::array initializer lists.
    # Warning C5045 involves Spectre mitigation, which I do not care for.
    add_compile_options(/Wall /wd5246 /wd5045 /GR-)
else(MSVC)
    add_compile_options(-Wall)
endif(MSVC)

find_package(Vulkan REQUIRED COMPONENTS glslc)
find_package(SDL2 CONFIG REQUIRED)
find_package(glm REQUIRED)

file(GLOB SOURCES CONFIGURE_DEPENDS src/*.c src/*.cpp)
file(GLOB HEADERS CONFIGURE_DEPENDS src/*.h src/*.hpp)
file(GLOB SHADERS CONFIGURE_DEPENDS src/shaders/*.glsl)
source_group("Shader Files" FILES ${SHADERS})

foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    set(SHADER_COMPILED "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SHADER_COMPILED}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/"
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SHADER} -o ${SHADER_COMPILED}
        DEPENDS ${SHADER}
    )
    list(APPEND SHADERS_COMPILED ${SHADER_COMPILED})
endforeach()
add_custom_target(vulkan-shaders DEPENDS ${SHADERS_COMPILED} SOURCES ${SHADERS})

add_definitions(-DNOMINMAX -DVULKAN_HPP_FLAGS_MASK_TYPE_AS_PUBLIC)
if(WIN32)
    add_definitions(-DVK_USE_PLATFORM_WIN32_KHR)
elseif(ANDROID)
    add_definitions(-DVK_USE_PLATFORM_ANDROID_KHR)
elseif(UNIX)
    add_definitions(-DVK_USE_PLATFORM_XCB_KHR)
else()
    message(FATAL_ERROR "Unsupported platform: CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
endif()
add_executable(vulkan-test ${SOURCES} ${HEADERS})
add_dependencies(vulkan-test vulkan-shaders)
add_custom_command(TARGET vulkan-test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:vulkan-test>/shaders/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/" "$<TARGET_FILE_DIR:vulkan-test>/shaders/"
)
target_include_directories(vulkan-test PRIVATE "src/")
target_link_libraries(vulkan-test Vulkan::Vulkan SDL2::SDL2 SDL2::SDL2main SDL2::SDL2-static glm::glm)
