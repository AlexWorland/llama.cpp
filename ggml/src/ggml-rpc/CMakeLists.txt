message(STATUS "Using RPC backend")

ggml_add_backend_library(ggml-rpc
                         ggml-rpc.cpp
                        )

if (WIN32)
    target_link_libraries(ggml-rpc PRIVATE ws2_32)
endif()

include(CheckIncludeFile)

option(GGML_RPC_RDMA "ggml: enable RDMA transport for RPC" OFF)
if (GGML_RPC_RDMA)
    if (APPLE)
        # macOS: dlopen("librdma.dylib") at runtime — no compile-time linkage.
        # Requires macOS SDK >= 26.2 for infiniband/verbs.h header.
        check_include_file("infiniband/verbs.h" HAVE_VERBS_H)
        if (NOT HAVE_VERBS_H)
            message(FATAL_ERROR "RDMA headers not found. Requires macOS SDK >= 26.2 (Tahoe)")
        endif()
        target_compile_definitions(ggml-rpc PRIVATE GGML_RPC_RDMA GGML_RPC_RDMA_DYNLOAD)
        # dlopen/dlsym are in libdl, which is implicit on macOS
        message(STATUS "  RDMA transport enabled (macOS Thunderbolt, dlopen)")
    else()
        # Linux: compile-time linkage against libibverbs
        find_library(IBVERBS_LIB ibverbs REQUIRED)
        target_compile_definitions(ggml-rpc PRIVATE GGML_RPC_RDMA)
        target_link_libraries(ggml-rpc PRIVATE ${IBVERBS_LIB})
        message(STATUS "  RDMA transport enabled (Linux libibverbs)")
    endif()
endif()
