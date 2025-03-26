cmake_minimum_required(VERSION 3.15)

# Define your project name and version
project(3BandMultiEffector VERSION 1.0.0)

# Set C++ standard (JUCE requires at least C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option: You can add some flags if needed
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Check that JUCE_DIR is set; if not, throw an error.
if (NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR "JUCE_DIR is not defined! Please set JUCE_DIR to the location of the JUCE repository.")
endif()

# Add JUCE as a subdirectory (this compiles the JUCE modules)
add_subdirectory(${JUCE_DIR} JUCE-build)

# Add your plugin using JUCE's CMake helper. Adjust the parameters as needed.
juce_add_plugin(3BandMultiEffector
    VERSION         "1.0.0"
    COMPANY_NAME    "ScottWu"
    PLUGIN_MANUFACTURER_CODE Juce  # 4-char code, adjust if needed
    PLUGIN_CODE     3BMf           # 4-char unique code for your plugin
    FORMATS         VST3
    PRODUCT_NAME    "3BandMultiEffector"
    BUNDLE_ID       "com.scottwu.3bandmultieffector"
)

# Add your plugin source files
target_sources(3BandMultiEffector PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    # Add any other source/header files here
)

# Optional: Define any preprocessor macros needed for your project
target_compile_definitions(3BandMultiEffector PRIVATE
    JUCE_WEB_BROWSER=0  # Example: disable the web browser module if not used
)
