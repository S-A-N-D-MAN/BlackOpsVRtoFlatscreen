# Host integration only; the source files beside this file stay byte-identical
# to the Phase 1 WaW beta.1 archive recorded in PROVENANCE.json.
add_library(bovr_scene_math STATIC
  stereo_scene_math.cpp camera_comfort_logic.cpp head_relative_pose_freeze_logic.cpp)
target_include_directories(bovr_scene_math PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bovr_scene_math PUBLIC BlackOpsVR::XR)
foreach(name IN ITEMS stereo_scene_math camera_comfort_logic head_relative_pose_freeze_logic)
  add_executable(bovr_${name}_tests "${name}_test.cpp")
  target_link_libraries(bovr_${name}_tests PRIVATE bovr_scene_math)
  add_test(NAME ${name} COMMAND bovr_${name}_tests)
endforeach()
