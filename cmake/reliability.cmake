# SPDX-License-Identifier: MIT

# Defer until ZMK has declared all app sources. This works in the standard ZMK
# reusable workflow too, without modifying its checkout or requiring a fork.
set_property(GLOBAL PROPERTY NOCFREE_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
function(nocfree_replace_zmk_sources)
  get_property(module GLOBAL PROPERTY NOCFREE_MODULE_DIR)
  get_filename_component(module "${module}" REALPATH)
  set(output "${CMAKE_BINARY_DIR}/nocfree-patched")
  execute_process(
    COMMAND ${PYTHON_EXECUTABLE} "${module}/scripts/prepare-zmk.py"
            "${APPLICATION_SOURCE_DIR}/src" "${output}"
    RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "NocFree reliability patch failed: ${log}${error}")
  endif()
  set(replaced ble.c hog.c keymap.c physical_layouts.c usb_hid.c usb.c
      split/bluetooth/central.c split/bluetooth/peripheral.c split/bluetooth/service.c)
  get_target_property(sources app SOURCES)
  set(updated)
  set(found)
  foreach(source IN LISTS sources)
    set(replacement "${source}")
    foreach(name IN LISTS replaced)
      if(source STREQUAL "${APPLICATION_SOURCE_DIR}/src/${name}" OR
         source STREQUAL "src/${name}")
        set(replacement "${output}/${name}")
        list(APPEND found "${name}")
      endif()
    endforeach()
    list(APPEND updated "${replacement}")
  endforeach()
  set(required physical_layouts.c)
  if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    list(APPEND required ble.c hog.c keymap.c usb_hid.c usb.c split/bluetooth/central.c)
  else()
    list(APPEND required split/bluetooth/peripheral.c split/bluetooth/service.c)
  endif()
  foreach(name IN LISTS required)
    if(NOT name IN_LIST found)
      message(FATAL_ERROR "Reliability source was not replaced: ${name}")
    endif()
  endforeach()
  set_property(TARGET app PROPERTY SOURCES "${updated}")
  target_include_directories(app PRIVATE "${module}/src/reliability"
      "${APPLICATION_SOURCE_DIR}/src" "${APPLICATION_SOURCE_DIR}/src/split/bluetooth")
  file(GLOB_RECURSE inputs CONFIGURE_DEPENDS
      "${module}/boards/*" "${module}/config/*" "${module}/drivers/*"
      "${module}/dts/*" "${module}/src/*" "${module}/cmake/*" "${module}/zephyr/*")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${inputs}
      "${module}/CMakeLists.txt" "${module}/Kconfig"
      "${module}/patches/zmk-reliability.patch" "${module}/patches/upstream-sha256.json"
      "${module}/scripts/prepare-zmk.py")
endfunction()
cmake_language(DEFER DIRECTORY "${APPLICATION_SOURCE_DIR}" CALL nocfree_replace_zmk_sources)
