# CMake toolchain: cross-compile Windows x64 (MSVC ABI) from Linux with clang-cl + lld-link,
# against the MSVC CRT + Windows SDK splatted by `xwin` into toolchain/xwin.
#
# Usage: cmake -G Ninja -S mod -B build -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchain/clang-cl-xwin.cmake  (absolute path)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(XWIN_DIR "${CMAKE_CURRENT_LIST_DIR}/xwin" CACHE PATH "xwin splat output directory")
if(NOT EXISTS "${XWIN_DIR}/crt/include" OR NOT EXISTS "${XWIN_DIR}/sdk/include/um")
  message(FATAL_ERROR "xwin splat not found at ${XWIN_DIR} (expected crt/ and sdk/)")
endif()

find_program(CLANG_CL_EXE clang-cl REQUIRED)
find_program(LLD_LINK_EXE lld-link REQUIRED)
find_program(LLVM_LIB_EXE llvm-lib REQUIRED)
find_program(LLVM_RC_EXE llvm-rc REQUIRED)
find_program(LLVM_MT_EXE llvm-mt)

set(CMAKE_C_COMPILER   "${CLANG_CL_EXE}")
set(CMAKE_CXX_COMPILER "${CLANG_CL_EXE}")
set(CMAKE_C_COMPILER_TARGET   x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_LINKER "${LLD_LINK_EXE}")
set(CMAKE_AR     "${LLVM_LIB_EXE}")
set(CMAKE_RC_COMPILER "${LLVM_RC_EXE}")
if(LLVM_MT_EXE)
  set(CMAKE_MT "${LLVM_MT_EXE}")
endif()

# /imsvc marks the paths as system headers (no warnings from the SDK).
set(_xwin_inc
  "/imsvc${XWIN_DIR}/crt/include"
  "/imsvc${XWIN_DIR}/sdk/include/ucrt"
  "/imsvc${XWIN_DIR}/sdk/include/um"
  "/imsvc${XWIN_DIR}/sdk/include/shared")
list(JOIN _xwin_inc " " _xwin_inc_str)
set(CMAKE_C_FLAGS_INIT   "--target=x86_64-pc-windows-msvc ${_xwin_inc_str}")
set(CMAKE_CXX_FLAGS_INIT "--target=x86_64-pc-windows-msvc ${_xwin_inc_str}")

set(_xwin_lib
  "/libpath:${XWIN_DIR}/crt/lib/x86_64"
  "/libpath:${XWIN_DIR}/sdk/lib/um/x86_64"
  "/libpath:${XWIN_DIR}/sdk/lib/ucrt/x86_64")
list(JOIN _xwin_lib " " _xwin_lib_str)
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_xwin_lib_str}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_lib_str}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_lib_str}")

# xwin ships no debug CRT (msvcrtd/libcmtd); CMake's compiler probe links a Debug /MDd exe.
# Probe with a static library instead (no link step). Projects must use the release CRT.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Never pick up host (Linux) libraries or headers.
set(CMAKE_FIND_ROOT_PATH "${XWIN_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
