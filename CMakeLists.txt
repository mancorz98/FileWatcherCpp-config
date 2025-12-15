cmake_minimum_required(VERSION 3.10)
project(FileWatcher)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add executable
add_executable(main main.cpp)
# Add include directories
target_include_directories(main PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)



# Find and link threads
find_package(Threads REQUIRED)
target_link_libraries(main PRIVATE Threads::Threads)




# Platform-specific settings
if(APPLE)
    # macOS frameworks
    target_link_libraries(main PRIVATE
        "-framework CoreFoundation"
        "-framework CoreServices"
    )
    
    # Suppress deprecation warnings
    target_compile_options(main PRIVATE -Wno-deprecated-declarations)
endif()

# Include directories (for FileWatch.hpp)
target_include_directories(main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})