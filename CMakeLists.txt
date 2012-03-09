cmake_minimum_required (VERSION 2.8)
project (flvtool-pp)

include_directories ( /usr/local/include )

add_executable (flvtollpp AMFData.cpp flvtool++.cpp)

