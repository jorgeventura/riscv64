cmake_minimum_required(VERSION 3.15)
project(riscv_vm_bringup C ASM)


# ====== CONFIGURABLE ARCH ======
# Default if user doesn't pass -DARCH_EXT=...
if(NOT DEFINED ARCH_EXT)
  set(ARCH_EXT "imac")        # pick what your toolchain supports: i / im / ima / imac / imafd / imafdc ...
endif()

# ====== Derive ABI from ARCH_EXT ======
string(TOLOWER "${ARCH_EXT}" _ext)
if(_ext MATCHES "d")
  set(ABI "lp64d")
elseif(_ext MATCHES "f")
  set(ABI "lp64f")
else()
  set(ABI "lp64")
endif()

# ====== ISA flags as a *list* (one flag per element) ======
set(ARCH_OPTS
  "-march=rv64${ARCH_EXT}_zicsr"   # if your toolchain is old and hates zicsr, drop "_zicsr"
  "-mabi=${ABI}"
  "-mcmodel=medany"
)

message(STATUS "Using: ${ARCH_OPTS}")



# ====== CONFIGURABLE ARCH ======
# Pick your CPU extensions here or at configure time:
#   cmake -DARCH_EXT=imac ..        # (default) no FPU, compressed
#   cmake -DARCH_EXT=imafd ..       # single+double FPU, no 'c'
#   cmake -DARCH_EXT=imafdc ..      # FPU + compressed
#   cmake -DARCH_EXT=ima ..         # no FPU, no 'c'
#
# This file will infer the correct ABI:
#   contains 'd' -> lp64d
#   else if contains 'f' -> lp64f
#   else -> lp64
#
# ====== Toolchain discovery (riscv64-unknown-linux-gnu-*) ======
set(TRIPLE riscv64-unknown-linux-gnu)

find_program(CROSS_GCC     ${TRIPLE}-gcc     REQUIRED)
find_program(CROSS_GXX     ${TRIPLE}-g++     REQUIRED)
find_program(CROSS_OBJCOPY ${TRIPLE}-objcopy REQUIRED)
find_program(CROSS_OBJDUMP ${TRIPLE}-objdump REQUIRED)
find_program(CROSS_STRIP   ${TRIPLE}-strip   REQUIRED)
find_program(CROSS_NM      ${TRIPLE}-nm      REQUIRED)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER   ${CROSS_GCC})
set(CMAKE_ASM_COMPILER ${CROSS_GCC})
set(CMAKE_C_STANDARD 99)


message(STATUS "Using -march=rv64${ARCH_EXT} -mabi=${ABI}")

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Debug/opt flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(OPT_FLAGS "-O0 -g3")
else()
  set(OPT_FLAGS "-O2 -g")
endif()

set(CMAKE_C_FLAGS   "${COMMON_ARCH_FLAGS} ${COMMON_CFLAGS} ${OPT_FLAGS}")
set(CMAKE_ASM_FLAGS "${COMMON_ARCH_FLAGS} ${OPT_FLAGS}")


# ====== Target Kernel ======
add_executable(kernel.elf
  src/start.S
  src/printf.c
  src/mmu.c
  src/kernel.c
)

target_compile_definitions(kernel.elf PRIVATE PRINTF_DISABLE_SUPPORT_FLOAT)
target_compile_options(kernel.elf PRIVATE ${ARCH_OPTS} -ffreestanding -fno-pic -fno-builtin -fno-plt -fno-asynchronous-unwind-tables)
target_link_options(   kernel.elf PRIVATE ${ARCH_OPTS} -nostdlib -nostartfiles -static -Wl,-n -Wl,-Map=${CMAKE_BINARY_DIR}/kernel.map -T ${CMAKE_SOURCE_DIR}/ldscripts/link.ld)

# Headers under include/
target_include_directories(kernel.elf PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

set_source_files_properties(${LINKER_SCRIPT} PROPERTIES HEADER_FILE_ONLY ON)

# Avoid host libs; keep static search behavior
set_target_properties(kernel.elf PROPERTIES
  LINK_SEARCH_START_STATIC ON
  LINK_SEARCH_END_STATIC   ON
)

# ====== Post-build: split symbols & raw bin ======
add_custom_command(TARGET kernel.elf POST_BUILD
  COMMAND ${CROSS_OBJCOPY} --only-keep-debug kernel.elf kernel.debug
  COMMAND ${CROSS_STRIP} -g kernel.elf
  COMMAND ${CROSS_OBJCOPY} --add-gnu-debuglink=kernel.debug kernel.elf
  COMMAND ${CROSS_OBJCOPY} -O binary kernel.elf kernel.bin
  COMMAND ${CROSS_OBJDUMP} -t kernel.debug > kernel.sym
  BYPRODUCTS kernel.debug kernel.bin kernel.sym
  COMMENT "Created kernel.debug (symbols), stripped kernel.elf, kernel.bin (raw), and kernel.sym (table)"
)

