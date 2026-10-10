# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{ADF_PATH}/examples/protocols/components/av_stream")
include($ENV{ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(internship)




