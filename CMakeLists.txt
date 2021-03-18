project("XyceSimulationServer" CXX)
cmake_minimum_required(VERSION 3.1)
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to be used")

find_package(CapnProto CONFIG REQUIRED)

capnp_generate_cpp(SimulatorSources SimulatorHeaders api/Simulator.capnp)
add_executable(XyceSimServer XyceSimServer.cpp ${SimulatorSources})
target_link_libraries(XyceSimServer PRIVATE CapnProto::capnp-rpc
xyce
ADMS
NeuronModels
teuchoscore 
)
target_include_directories(XyceSimServer PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
install(TARGETS XyceSimServer)
