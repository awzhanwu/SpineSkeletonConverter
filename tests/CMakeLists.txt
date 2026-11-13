cmake_minimum_required(VERSION 3.20)

include(FetchContent)
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Keep the runtime and its editor-exported fixtures on the same revision.
set(SPINE43_REVISION 405f43295b5832e86d9df3a80beb6bc9df772312)
set(SPINE_SET_COMPILER_FLAGS OFF CACHE INTERNAL "Use the test toolchain flags for spine-cpp")
FetchContent_Declare(spine43_runtime
    URL "https://codeload.github.com/EsotericSoftware/spine-runtimes/tar.gz/${SPINE43_REVISION}"
    URL_HASH SHA256=4d85442167b06b4bf2a307af5f17862ecf4b5acbd253ecb09e895c77f30d15b5
    SOURCE_SUBDIR spine-cpp
)
FetchContent_MakeAvailable(spine43_runtime)

add_executable(Spine43RuntimeCheck Spine43RuntimeCheck.cpp)
target_link_libraries(Spine43RuntimeCheck PRIVATE spine-cpp)
target_include_directories(Spine43RuntimeCheck PRIVATE "${PROJECT_SOURCE_DIR}/include")

# This official 3.8 revision can read 3.8.75 exports directly. Later revisions
# reject that patch version because some editor exports were corrupt.
FetchContent_Declare(spine38_reference
    URL "https://codeload.github.com/EsotericSoftware/spine-runtimes/tar.gz/c0699e23a0c8799710323bdf0e076e18f6ba41a2"
    URL_HASH SHA256=dde758e92c817e93fec380db5998ebea375195b2d0c4b9c4f8a7029417d06d31
    SOURCE_SUBDIR spine-cpp/spine-cpp/src
)
FetchContent_MakeAvailable(spine38_reference)
set(spine38_cpp "${spine38_reference_SOURCE_DIR}/spine-cpp/spine-cpp")
file(GLOB spine38_sources "${spine38_cpp}/src/spine/*.cpp")
add_library(spine38-runtime STATIC ${spine38_sources})
target_include_directories(spine38-runtime PUBLIC "${spine38_cpp}/include")

add_executable(Spine38PoseSample SpinePoseSample.cpp)
target_compile_definitions(Spine38PoseSample PRIVATE SPINE_REFERENCE_38)
target_link_libraries(Spine38PoseSample PRIVATE spine38-runtime)
target_include_directories(Spine38PoseSample PRIVATE "${PROJECT_SOURCE_DIR}/include")

add_executable(Spine43PoseSample SpinePoseSample.cpp)
target_link_libraries(Spine43PoseSample PRIVATE spine-cpp)
target_include_directories(Spine43PoseSample PRIVATE "${PROJECT_SOURCE_DIR}/include")

function(spine43_roundtrip name input)
    add_test(NAME spine43.${name} COMMAND "${CMAKE_COMMAND}"
        "-DCONVERTER=$<TARGET_FILE:SpineSkeletonDataConverter>"
        "-DVERIFIER=$<TARGET_FILE:Spine43RuntimeCheck>"
        "-DINPUT=${input}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name}"
        ${ARGN}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RoundTrip.cmake"
    )
    set_tests_properties(spine43.${name} PROPERTIES TIMEOUT 60)
endfunction()

foreach(example IN ITEMS spineboy mix-and-match goblins celestial-circus tank raptor coin)
    set(export "${spine43_runtime_SOURCE_DIR}/examples/${example}/export/${example}-pro")
    spine43_roundtrip(${example} "${export}.json" "-DOFFICIAL_SKEL=${export}.skel")
endforeach()

spine43_roundtrip(features "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/spine43-features.json")
spine43_roundtrip(minimal "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/spine43-minimal.json")
spine43_roundtrip(no-default-skin "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/spine43-no-default.spine-json")
spine43_roundtrip(spine38 "${PROJECT_SOURCE_DIR}/data/38/spineboy/export/spineboy-pro.skel" -DUPGRADE=ON)
spine43_roundtrip(spine38-raptor "${PROJECT_SOURCE_DIR}/data/38/raptor/export/raptor-pro.skel" -DUPGRADE=ON)
spine43_roundtrip(legacy-writer "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/spine43-legacy.skel" -DUPGRADE=ON)

function(spine38_poses name input)
    add_test(NAME spine38.${name} COMMAND "${CMAKE_COMMAND}"
        "-DCONVERTER=$<TARGET_FILE:SpineSkeletonDataConverter>"
        "-DVERIFIER=$<TARGET_FILE:Spine43RuntimeCheck>"
        "-DSAMPLER38=$<TARGET_FILE:Spine38PoseSample>"
        "-DSAMPLER43=$<TARGET_FILE:Spine43PoseSample>"
        "-DPYTHON=${Python3_EXECUTABLE}"
        "-DINPUT=${input}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name}"
        ${ARGN}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CompareSpinePoses.cmake"
    )
    set_tests_properties(spine38.${name} PROPERTIES TIMEOUT 60)
endfunction()

set(local_transforms "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/spine38-local-transforms.json")
spine38_poses(local-transform-json "${local_transforms}" -DSKIN=extra)
spine38_poses(local-transform-skel "${local_transforms}" -DBINARY_INPUT=ON -DSKIN=extra)
spine38_poses(local-transform-skel-no-skin "${local_transforms}" -DBINARY_INPUT=ON)
# This editor export has IK and no transform constraints. It is also a control
# for the pre-existing cross-runtime position tolerance on its larger rig.
spine38_poses(ik-only "${PROJECT_SOURCE_DIR}/data/38/raptor/export/raptor-pro.skel" -DPOSITION_TOLERANCE=0.002)
