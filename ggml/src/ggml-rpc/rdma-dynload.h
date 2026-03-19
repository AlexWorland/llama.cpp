// Runtime loading of libibverbs — follows Apple MLX/JACCL dlopen pattern.
// On macOS: loads librdma.dylib (umbrella, includes thunderboltrdma provider).
// On Linux: loads libibverbs.so at runtime when GGML_RPC_RDMA_DYNLOAD is set.
//
// Ref: https://github.com/ml-explore/mlx/blob/main/mlx/distributed/jaccl/utils.cpp
// Ref: https://github.com/exo-explore/exo (Thunderbolt RDMA network setup)

#pragma once

#ifdef GGML_RPC_RDMA

#include <infiniband/verbs.h>
#include <dlfcn.h>
#include <cstdio>

#define RDMA_LOAD_SYM(handle, sym, var) do { \
    (var) = reinterpret_cast<decltype(var)>(dlsym(handle, #sym)); \
    if (!(var)) { \
        fprintf(stderr, "RDMA dynload: failed to resolve " #sym ": %s\n", dlerror()); \
        dlclose(handle); handle = nullptr; return false; \
    } \
} while(0)

struct rdma_dynlib {
    void * handle = nullptr;

    // Device management
    decltype(&ibv_get_device_list)  fn_get_device_list  = nullptr;
    decltype(&ibv_get_device_name)  fn_get_device_name  = nullptr;
    decltype(&ibv_open_device)      fn_open_device      = nullptr;
    decltype(&ibv_close_device)     fn_close_device     = nullptr;
    decltype(&ibv_free_device_list) fn_free_device_list = nullptr;

    // Protection domain
    decltype(&ibv_alloc_pd)         fn_alloc_pd         = nullptr;
    decltype(&ibv_dealloc_pd)       fn_dealloc_pd       = nullptr;

    // Completion queue
    decltype(&ibv_create_cq)        fn_create_cq        = nullptr;
    decltype(&ibv_destroy_cq)       fn_destroy_cq       = nullptr;

    // Queue pair
    decltype(&ibv_create_qp)        fn_create_qp        = nullptr;
    decltype(&ibv_destroy_qp)       fn_destroy_qp       = nullptr;
    decltype(&ibv_modify_qp)        fn_modify_qp        = nullptr;

    // Memory registration
    decltype(&ibv_reg_mr)           fn_reg_mr           = nullptr;
    decltype(&ibv_dereg_mr)         fn_dereg_mr         = nullptr;

    // Port / GID queries
    decltype(&ibv_query_port)       fn_query_port       = nullptr;
    decltype(&ibv_query_gid)        fn_query_gid        = nullptr;

    // Status strings
    decltype(&ibv_wc_status_str)    fn_wc_status_str    = nullptr;

    // NOTE: ibv_post_send, ibv_post_recv, ibv_poll_cq are typically inlined
    // macros/functions in verbs.h that dispatch through qp->context->ops.
    // They work once the context is valid — they don't need dlsym.

    bool load() {
        if (handle) return true;

#ifdef __APPLE__
        handle = dlopen("librdma.dylib", RTLD_NOW | RTLD_GLOBAL);
#else
        handle = dlopen("libibverbs.so", RTLD_NOW);
        if (!handle) handle = dlopen("libibverbs.so.1", RTLD_NOW);
#endif
        if (!handle) {
            return false;
        }

        RDMA_LOAD_SYM(handle, ibv_get_device_list,  fn_get_device_list);
        RDMA_LOAD_SYM(handle, ibv_get_device_name,  fn_get_device_name);
        RDMA_LOAD_SYM(handle, ibv_open_device,      fn_open_device);
        RDMA_LOAD_SYM(handle, ibv_close_device,     fn_close_device);
        RDMA_LOAD_SYM(handle, ibv_free_device_list, fn_free_device_list);
        RDMA_LOAD_SYM(handle, ibv_alloc_pd,         fn_alloc_pd);
        RDMA_LOAD_SYM(handle, ibv_dealloc_pd,       fn_dealloc_pd);
        RDMA_LOAD_SYM(handle, ibv_create_cq,        fn_create_cq);
        RDMA_LOAD_SYM(handle, ibv_destroy_cq,       fn_destroy_cq);
        RDMA_LOAD_SYM(handle, ibv_create_qp,        fn_create_qp);
        RDMA_LOAD_SYM(handle, ibv_destroy_qp,       fn_destroy_qp);
        RDMA_LOAD_SYM(handle, ibv_modify_qp,        fn_modify_qp);
        RDMA_LOAD_SYM(handle, ibv_reg_mr,           fn_reg_mr);
        RDMA_LOAD_SYM(handle, ibv_dereg_mr,         fn_dereg_mr);
        RDMA_LOAD_SYM(handle, ibv_query_port,       fn_query_port);
        RDMA_LOAD_SYM(handle, ibv_query_gid,        fn_query_gid);
        RDMA_LOAD_SYM(handle, ibv_wc_status_str,    fn_wc_status_str);

        return true;
    }

    ~rdma_dynlib() {
        // Don't dlclose — some drivers don't handle unload cleanly
    }
};

static inline rdma_dynlib & rdma_lib() {
    static rdma_dynlib lib;
    return lib;
}

#endif // GGML_RPC_RDMA
