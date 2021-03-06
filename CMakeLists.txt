cmake_minimum_required(VERSION 2.8)

PROJECT(gseg_pcd_tools)

SET(CMAKE_CXX_FLAGS "-O3 -std=c++11 -w -fopenmp -pthread")
SET(CMAKE_C_FLAGS "-O3 -std=c++11 -fopenmp -pthread")

#SET(CMAKE_BUILD_TYPE "Debug") 
#SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")

OPTION(BUILD_WITH_LIBLAS "Build with LIBLAS for *.LAS point cloud data IO" OFF)

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

#PCL (compulsory)
FIND_PACKAGE(PCL REQUIRED)
INCLUDE_DIRECTORIES(${PCL_INCLUDE_DIRS})
ADD_DEFINITIONS(${PCL_DEFINITIONS})
LIST(REMOVE_ITEM PCL_LIBRARIES "vtkproj4")
MESSAGE("PCL [OK]")


SET(DEP_LIBS ${DEP_LIBS} ${PCL_LIBRARIES})

IF(BUILD_WITH_LIBLAS)
FIND_PACKAGE(LIBLAS REQUIRED QUIET)
IF(LIBLAS_FOUND) 
INCLUDE_DIRECTORIES(${LIBLAS_INCLUDE_DIR})
MESSAGE("LIBLAS [OK]: " ${LIBLAS_INCLUDE_DIR}) 
ADD_DEFINITIONS(-DLIBLAS_ON)
SET(DEP_LIBS ${DEP_LIBS} ${LIBLAS_LIBRARY} liblas.so.2.4.0)
ENDIF(LIBLAS_FOUND)
ENDIF(BUILD_WITH_LIBLAS)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/include)

ADD_EXECUTABLE(gseg_pcdcut ${PROJECT_SOURCE_DIR}/test/gseg_pcdcut.cpp)
TARGET_LINK_LIBRARIES(gseg_pcdcut ${DEP_LIBS})

ADD_EXECUTABLE(gseg_pcd2grid ${PROJECT_SOURCE_DIR}/test/gseg_pcd2grid.cpp)
TARGET_LINK_LIBRARIES(gseg_pcd2grid ${DEP_LIBS})
