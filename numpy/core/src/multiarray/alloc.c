#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "structmember.h"

#define NPY_NO_DEPRECATED_API NPY_API_VERSION
#define _MULTIARRAYMODULE
#include <numpy/ndarraytypes.h>
#include "numpy/arrayobject.h"
#include <numpy/npy_common.h>
#include "npy_config.h"
#include "templ_common.h" /* for npy_mul_with_overflow_intp */

#include <assert.h>

#define NBUCKETS 1024 /* number of buckets for data*/
#define NBUCKETS_DIM 16 /* number of buckets for dimensions/strides */
#define NCACHE 7 /* number of cache entries per bucket */
/* this structure fits neatly into a cacheline */
typedef struct {
    npy_uintp available; /* number of cached pointers */
    void * ptrs[NCACHE];
} cache_bucket;
static cache_bucket datacache[NBUCKETS];
static cache_bucket dimcache[NBUCKETS_DIM];

/*
 * very simplistic small memory block cache to avoid more expensive libc
 * allocations
 * base function for data cache with 1 byte buckets and dimension cache with
 * sizeof(npy_intp) byte buckets
 */
static NPY_INLINE void *
_npy_alloc_cache(npy_uintp nelem, npy_uintp esz, npy_uint msz,
                 cache_bucket * cache, void * (*alloc)(size_t))
{
    assert((esz == 1 && cache == datacache) ||
           (esz == sizeof(npy_intp) && cache == dimcache));
    if (nelem < msz) {
        if (cache[nelem].available > 0) {
            return cache[nelem].ptrs[--(cache[nelem].available)];
        }
    }
    return alloc(nelem * esz);
}

/*
 * return pointer p to cache, nelem is number of elements of the cache bucket
 * size (1 or sizeof(npy_intp)) of the block pointed too
 */
static NPY_INLINE void
_npy_free_cache(void * p, npy_uintp nelem, npy_uint msz,
                cache_bucket * cache, void (*dealloc)(void *))
{
    if (p != NULL && nelem < msz) {
        if (cache[nelem].available < NCACHE) {
            cache[nelem].ptrs[cache[nelem].available++] = p;
            return;
        }
    }
    dealloc(p);
}

/*
 * clear all cache data in the given cache
 */
static void
_npy_clear_cache(npy_uint msz, cache_bucket * cache, void (*dealloc)(void *))
{
    npy_intp i, nelem;
    for (nelem = 0; nelem < msz; nelem++) {
        for (i = 0; i < cache[nelem].available; i++) {
            dealloc(cache[nelem].ptrs[i]);
        }
        cache[nelem].available = 0;
    }
}

/*
 * array data cache, sz is number of bytes to allocate
 */
NPY_NO_EXPORT void *
npy_alloc_cache(npy_uintp sz)
{
    return _npy_alloc_cache(sz, 1, NBUCKETS, datacache, &PyDataMem_NEW);
}

/* zero initialized data, sz is number of bytes to allocate */
NPY_NO_EXPORT void *
npy_alloc_cache_zero(npy_uintp sz)
{
    void * p;
    NPY_BEGIN_THREADS_DEF;
    if (sz < NBUCKETS) {
        p = _npy_alloc_cache(sz, 1, NBUCKETS, datacache, &PyDataMem_NEW);
        if (p) {
            memset(p, 0, sz);
        }
        return p;
    }
    NPY_BEGIN_THREADS;
    p = PyDataMem_NEW_ZEROED(sz, 1);
    NPY_END_THREADS;
    return p;
}

NPY_NO_EXPORT void
npy_free_cache(void * p, npy_uintp sz)
{
    _npy_free_cache(p, sz, NBUCKETS, datacache, &PyDataMem_FREE);
}

/*
 * dimension/stride cache, uses a different allocator and is always a multiple
 * of npy_intp
 */
NPY_NO_EXPORT void *
npy_alloc_cache_dim(npy_uintp sz)
{
    /* dims + strides */
    if (NPY_UNLIKELY(sz < 2)) {
        sz = 2;
    }
    return _npy_alloc_cache(sz, sizeof(npy_intp), NBUCKETS_DIM, dimcache,
                            &PyArray_malloc);
}

NPY_NO_EXPORT void
npy_free_cache_dim(void * p, npy_uintp sz)
{
    /* dims + strides */
    if (NPY_UNLIKELY(sz < 2)) {
        sz = 2;
    }
    _npy_free_cache(p, sz, NBUCKETS_DIM, dimcache,
                    &PyArray_free);
}


/* malloc/free/realloc hook */
NPY_NO_EXPORT PyDataMem_EventHookFunc *_PyDataMem_eventhook;
NPY_NO_EXPORT void *_PyDataMem_eventhook_user_data;

/*NUMPY_API
 * Sets the allocation event hook for numpy array data.
 * Takes a PyDataMem_EventHookFunc *, which has the signature:
 *        void hook(void *old, void *new, size_t size, void *user_data).
 *   Also takes a void *user_data, and void **old_data.
 *
 * Returns a pointer to the previous hook or NULL.  If old_data is
 * non-NULL, the previous user_data pointer will be copied to it.
 *
 * If not NULL, hook will be called at the end of each PyDataMem_NEW/FREE/RENEW:
 *   result = PyDataMem_NEW(size)        -> (*hook)(NULL, result, size, user_data)
 *   PyDataMem_FREE(ptr)                 -> (*hook)(ptr, NULL, 0, user_data)
 *   result = PyDataMem_RENEW(ptr, size) -> (*hook)(ptr, result, size, user_data)
 *
 * When the hook is called, the GIL will be held by the calling
 * thread.  The hook should be written to be reentrant, if it performs
 * operations that might cause new allocation events (such as the
 * creation/descruction numpy objects, or creating/destroying Python
 * objects which might cause a gc)
 */
NPY_NO_EXPORT PyDataMem_EventHookFunc *
PyDataMem_SetEventHook(PyDataMem_EventHookFunc *newhook,
                       void *user_data, void **old_data)
{
    PyDataMem_EventHookFunc *temp;
    NPY_ALLOW_C_API_DEF
    NPY_ALLOW_C_API
    temp = _PyDataMem_eventhook;
    _PyDataMem_eventhook = newhook;
    if (old_data != NULL) {
        *old_data = _PyDataMem_eventhook_user_data;
    }
    _PyDataMem_eventhook_user_data = user_data;
    NPY_DISABLE_C_API
    return temp;
}


/* Choose a minimum valid alignment for common data types */
#define MIN_ALIGN 16

static size_t datamem_align = MIN_ALIGN;
static size_t datamem_align_mask = MIN_ALIGN - 1;

static NPY_INLINE size_t
get_aligned_size(size_t size)
{
    return size + sizeof(void *) + datamem_align_mask;
}

/*
 * Align the given pointer to the guaranteed alignment, and remember
 * the original pointer.
 */
static NPY_INLINE void *
align_pointer(void *ptr)
{
    /* Ensure a pointer can fit in the space before */
    npy_intp aligned_ptr = ((npy_intp) ptr + sizeof(void *) + datamem_align_mask)
                           & ~datamem_align_mask;
    ((void **) aligned_ptr)[-1] = ptr;
    return (void *) aligned_ptr;
}

/*
 * Given an aligned pointer, get the start of the original allocation.
 */
static NPY_INLINE void *
get_original_pointer(void *aligned_ptr)
{
    return ((void **) aligned_ptr)[-1];
}

NPY_NO_EXPORT size_t
npy_datamem_get_align(void)
{
    return datamem_align;
}

NPY_NO_EXPORT int
npy_datamem_set_align(size_t align)
{
    size_t align_mask = align - 1;
    if (align < MIN_ALIGN) {
        /* Too small */
        return -1;
    }
    if ((align ^ align_mask) != (align | align_mask)) {
        /* Not a power of two */
        return -1;
    }
    if (align > datamem_align) {
        /* Alignment has increased, free all cached data areas as they may
           not be aligned anymore. */
        _npy_clear_cache(NBUCKETS, datacache, &PyDataMem_FREE);
    }
    datamem_align = align;
    datamem_align_mask = align_mask;
    return 0;
}

/*NUMPY_API
 * Allocates memory for array data.
 */
NPY_NO_EXPORT void *
PyDataMem_NEW(size_t size)
{
    void *result;

    result = malloc(get_aligned_size(size));
    if (result != NULL)
        result = align_pointer(result);

    if (_PyDataMem_eventhook != NULL) {
        NPY_ALLOW_C_API_DEF
        NPY_ALLOW_C_API
        if (_PyDataMem_eventhook != NULL) {
            (*_PyDataMem_eventhook)(NULL, result, size,
                                    _PyDataMem_eventhook_user_data);
        }
        NPY_DISABLE_C_API
    }
    return result;
}

/*NUMPY_API
 * Allocates zeroed memory for array data.
 */
NPY_NO_EXPORT void *
PyDataMem_NEW_ZEROED(size_t nelems, size_t elsize)
{
    void *result;
    size_t size;

    if (npy_mul_with_overflow_intp(&size, nelems, elsize))
        result = NULL;
    else {
        result = calloc(get_aligned_size(size), 1);
        if (result != NULL)
            result = align_pointer(result);
    }

    if (_PyDataMem_eventhook != NULL) {
        NPY_ALLOW_C_API_DEF
        NPY_ALLOW_C_API
        if (_PyDataMem_eventhook != NULL) {
            (*_PyDataMem_eventhook)(NULL, result, nelems * elsize,
                                    _PyDataMem_eventhook_user_data);
        }
        NPY_DISABLE_C_API
    }
    return result;
}

/*NUMPY_API
 * Free memory for array data.
 */
NPY_NO_EXPORT void
PyDataMem_FREE(void *ptr)
{
    if (ptr != NULL)
        free(get_original_pointer(ptr));
    if (_PyDataMem_eventhook != NULL) {
        NPY_ALLOW_C_API_DEF
        NPY_ALLOW_C_API
        if (_PyDataMem_eventhook != NULL) {
            (*_PyDataMem_eventhook)(ptr, NULL, 0,
                                    _PyDataMem_eventhook_user_data);
        }
        NPY_DISABLE_C_API
    }
}

/*NUMPY_API
 * Reallocate/resize memory for array data.
 */
NPY_NO_EXPORT void *
PyDataMem_RENEW(void *ptr, size_t size)
{
    void *base_result, *result;
    void *original_ptr = get_original_pointer(ptr);

    base_result = realloc(original_ptr, get_aligned_size(size));
    if (base_result == NULL)
        result = NULL;
    else {
        if (base_result == original_ptr)
            result = ptr;
        else {
            size_t offset = (npy_intp) ptr - (npy_intp) original_ptr;
            size_t new_offset;
            result = align_pointer(base_result);
            /* If the offset from base pointer changed, we must move
               the data area ourselves */
            new_offset = (npy_intp) result - (npy_intp) base_result;
            if (new_offset != offset)
                memmove(result, (const char *) base_result + offset, size);
        }
    }
    if (_PyDataMem_eventhook != NULL) {
        NPY_ALLOW_C_API_DEF
        NPY_ALLOW_C_API
        if (_PyDataMem_eventhook != NULL) {
            (*_PyDataMem_eventhook)(ptr, result, size,
                                    _PyDataMem_eventhook_user_data);
        }
        NPY_DISABLE_C_API
    }
    return result;
}
