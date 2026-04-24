if(NOT DEFINED SRC_DIR)
  message(FATAL_ERROR "SRC_DIR not provided")
endif()

if(NOT DEFINED LINT_CORE)
  set(LINT_CORE ON)
endif()

if(NOT DEFINED LINT_GAME_CAFE)
  set(LINT_GAME_CAFE ON)
endif()

if(NOT DEFINED LINT_TESTBED)
  set(LINT_TESTBED OFF)
endif()

if(NOT DEFINED LINT_ENGINE_PORTABILITY)
  set(LINT_ENGINE_PORTABILITY ON)
endif()

if(NOT DEFINED GAME_CAFE_DIR)
  set(GAME_CAFE_DIR "${SRC_DIR}/prototypes/cafe-tycoon-proto")
endif()

if(DEFINED RG_EXE AND NOT RG_EXE STREQUAL "")
  set(SEARCH_CMD "${RG_EXE}")
  set(SEARCH_HAS_REGEX ON)
else()
  set(SEARCH_CMD grep)
  set(SEARCH_HAS_REGEX OFF)
endif()

function(miso_boundary_lint include_pattern violation_message)
  set(options)
  set(oneValueArgs ALLOW_PATTERN)
  set(multiValueArgs PATHS)
  cmake_parse_arguments(LINT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(SEARCH_HAS_REGEX)
    execute_process(
      COMMAND "${SEARCH_CMD}" -n "${include_pattern}" ${LINT_PATHS}
      RESULT_VARIABLE lint_rc
      OUTPUT_VARIABLE lint_out
      ERROR_VARIABLE lint_err
    )
  else()
    execute_process(
      COMMAND "${SEARCH_CMD}" -R -n -E "${include_pattern}" ${LINT_PATHS}
      RESULT_VARIABLE lint_rc
      OUTPUT_VARIABLE lint_out
      ERROR_VARIABLE lint_err
    )
  endif()

  if(lint_rc EQUAL 0)
    set(filtered_out "${lint_out}")
    if(DEFINED LINT_ALLOW_PATTERN AND NOT LINT_ALLOW_PATTERN STREQUAL "")
      string(REPLACE "\n" ";" lint_lines "${lint_out}")
      set(filtered_lines)
      foreach(line IN LISTS lint_lines)
        if(line STREQUAL "")
          continue()
        endif()
        if(NOT line MATCHES "${LINT_ALLOW_PATTERN}")
          list(APPEND filtered_lines "${line}")
        endif()
      endforeach()

      if(filtered_lines)
        string(JOIN "\n" filtered_out ${filtered_lines})
      else()
        set(filtered_out "")
      endif()
    endif()

    if(NOT filtered_out STREQUAL "")
      message(FATAL_ERROR "${violation_message}\n${filtered_out}")
    endif()
  endif()
endfunction()

miso_boundary_lint(
  "#include[[:space:]]+\"renderer/renderer.h\""
  "Boundary violation: renderer/renderer.h is private outside the backend bridge"
  PATHS "${SRC_DIR}/engine/include" "${SRC_DIR}/engine/src" "${SRC_DIR}/testbed" "${SRC_DIR}/main.c"
  ALLOW_PATTERN ".*/engine/src/internal/miso__renderer_backend.c:"
)

if(LINT_CORE)
  miso_boundary_lint(
    "#include[[:space:]]+\"miso_cafe_"
    "Boundary violation: miso_core includes miso_cafe_* headers"
    PATHS "${SRC_DIR}/engine/include" "${SRC_DIR}/engine/src"
  )

  miso_boundary_lint(
    "#include[[:space:]]+\"testbed/"
    "Boundary violation: miso_core includes testbed headers"
    PATHS "${SRC_DIR}/engine/include" "${SRC_DIR}/engine/src"
  )
endif()

if(LINT_GAME_CAFE)
  if(EXISTS "${GAME_CAFE_DIR}")
    miso_boundary_lint(
      "#include[[:space:]]+\".*engine/src/internal/"
      "Boundary violation: miso_game_cafe includes engine internals"
      PATHS "${GAME_CAFE_DIR}/include" "${GAME_CAFE_DIR}/src"
    )
  else()
    message(STATUS "Skipping miso_game_cafe boundary lint; GAME_CAFE_DIR not found: ${GAME_CAFE_DIR}")
  endif()
endif()

if(LINT_TESTBED)
  miso_boundary_lint(
    "#include[[:space:]]+\".*engine/src/internal/"
    "Boundary violation: testbed includes engine internals"
    PATHS "${SRC_DIR}/testbed"
  )

  miso_boundary_lint(
    "#include[[:space:]]+\"miso_cafe_"
    "Boundary violation: testbed includes miso_cafe_* headers"
    PATHS "${SRC_DIR}/testbed"
  )
endif()

if(LINT_ENGINE_PORTABILITY)
  miso_boundary_lint(
    "(^|[^A-Za-z0-9_])(memcpy|memset|memcmp)[[:space:]]*\\("
    "Engine portability rule violation: use SDL_mem* wrappers in miso_core"
    PATHS "${SRC_DIR}/engine/src" "${SRC_DIR}/engine/include"
  )
endif()

message(STATUS "miso boundary lint passed")
