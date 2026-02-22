#
# Copyright (c) 2016-2020 Jean Davy
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
add_example(header_euml benchmark_header_euml euml.cpp)
if (EXISTS "${Boost_INCLUDE_DIRS}/boost/msm/front/euml2/euml2.hpp")
  add_executable(header_euml2 euml2.cpp)
  target_link_libraries(header_euml2 PRIVATE sml Boost::boost)
else()
  message(STATUS "Skipping header_euml2 benchmark: boost/msm/front/euml2/euml2.hpp not found")
endif()
add_example(header_sc benchmark_header_sc sc.cpp)
add_example(header_sml benchmark_header_sml sml.cpp)
