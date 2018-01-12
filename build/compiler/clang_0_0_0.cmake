include("${CMAKE_CURRENT_SOURCE_DIR}/build/compiler/shared.cmake")
set(COMPILER_CFLAGS "${SHARED_CFLAGS} -Wstrict-overflow=5")
set(COMPILER_CXXFLAGS "-nostdinc++ -I/usr/include/c++/v1 ${SHARED_CXXFLAGS} -Wstrict-overflow=5")
if("${LIB_PSS_VM_STACK_LIMIT}" STREQUAL "")
	set(LIB_PSS_VM_STACK_LIMIT 512)
endif("${LIB_PSS_VM_STACK_LIMIT}" STREQUAL "")
if("${STACK_SIZE}" STREQUAL "")
	set(STACK_SIZE "0x800000")
endif("${STACK_SIZE}" STREQUAL "")
