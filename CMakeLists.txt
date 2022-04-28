# SPDX-FileCopyrightText: 2022 Pepijn de Vos
#
# SPDX-License-Identifier: GPL-3.0-or-later

project("XyceSimulationServer" CXX)
cmake_minimum_required(VERSION 3.1)
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to be used")

find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(CapnProto CONFIG REQUIRED)
find_library(LIBXYCE xyce REQUIRED)

capnp_generate_cpp(SimulatorSources SimulatorHeaders api/Simulator.capnp)

add_executable(XyceSimServer api/main.cpp ${SimulatorSources})
target_link_libraries(XyceSimServer PRIVATE CapnProto::capnp-rpc
${LIBXYCE}
# teuchoscore
${CMAKE_DL_LIBS} ${Boost_LIBRARIES}
)
target_include_directories(XyceSimServer PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_PREFIX_PATH}/include ${Boost_INCLUDE_DIRS})
install(TARGETS XyceSimServer)
