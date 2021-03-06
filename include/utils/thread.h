/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the thread utilites
 * @file utils/thread.h
 **/
#include <utils/static_assertion.h>
#include <predict.h>
#ifndef __PLUMBER_UTILS_THREAD_H__
#define __PLUMBER_UTILS_THREAD_H__

/**
 * @brief represent the type of the thread
 * @note  we don't have a dispatcher here, thus the dispatcher should be treated as generic
 *        This is ok because the dispatcher is only the thread we dispatch the request, so
 *        we won't call any memory pool unitilies.
 **/
typedef enum {
	THREAD_TYPE_GENERIC     = 0,   /*!< A generic thread */
	THREAD_TYPE_EVENT       = 1,   /*!< A event loop thread */
	THREAD_TYPE_WORKER      = 2,   /*!< A worker thread */
	THREAD_TYPE_IO          = 4,   /*!< An IO thread */
	THREAD_TYPE_ASYNC       = 8,   /*!< An async task processing thread */
	THREAD_TYPE_MAX                /*!< The max bound of the thread type code */
} thread_type_t;

/**
 * @brief count if the thread type contains this bit
 **/
#define _THREAD_NUM_COUNT_BIT(x) ((unsigned long long)THREAD_TYPE_MAX > (unsigned long long)(x))

/**
 * @brief count how many bits are there valid
 **/
#define _THREAD_NUM_COUNT_BYTE(b) (_THREAD_NUM_COUNT_BIT(b) + _THREAD_NUM_COUNT_BIT(b * 2ull) + \
                                   _THREAD_NUM_COUNT_BIT(b * 4ull) + _THREAD_NUM_COUNT_BIT(b * 8ull) + \
                                   _THREAD_NUM_COUNT_BIT(b * 16ull) + _THREAD_NUM_COUNT_BIT(b * 32ull) + \
                                   _THREAD_NUM_COUNT_BIT(b * 64ull) + _THREAD_NUM_COUNT_BIT(b * 128ull))

/**
 * @brief how many thread types is valid in the system, not incluing the generic
 **/
#define THREAD_NUM_TYPES (_THREAD_NUM_COUNT_BYTE(1) + _THREAD_NUM_COUNT_BYTE(0x100) + _THREAD_NUM_COUNT_BYTE(0x10000) + _THREAD_NUM_COUNT_BYTE(0x1000000))

/**
 * @brief represent a thread object
 **/
typedef struct _thread_t thread_t;

/**
 * @brief The main function used for the testing envionment
 * @note This function should only used for testing purpose <br/>
 *       Because when we enabled the aligned stack by specifying the size of
 *       the stack, we will be able to use a simple integer arithmetic to figure out
 *       the thread id, which is light-weight. <br/>
 *       However, the price we have to pay for doing this, is that the main thread is
 *       an exception and if you called the get_thread_id then you will be in trouble.<br/>
 *       This is not a big issue in libplumber code, since the main thread is converted
 *       to dispatcher thread and do not use any thread utilities at all. <br/>
 *       However, in the test cases, there's a lot of example of calling the thread related
 *       utils from the main thread, which causes crash. <br/>
 *       To address this, we have to make the main function for the test cases using an aligned
 *       stack. And this is the way for us to initialize a thread with aligned stack at this point
 **/
typedef int (*thread_test_main_t)(void);

/**
 * @brief the function type for the main function of a thread
 * @param data the user-defined data
 * @return the result
 **/
typedef void* (*thread_main_t)(void* data);

/**
 * @brief the function type for the cleanup hooks
 * @param thread_data the thread startup data
 * @param cleanup_data the cleanup hook data
 * @return status code
 **/
typedef int (*thread_cleanup_t)(void* thread_data, void* cleanup_data);

/**
 * @brief the callback function that is used when the object needs to create a new pointer to the new thread
 * @param tid the thread id
 * @param data the additional data passed in to the allocator
 * @return the pointer to the new memeory, NULL on error cases
 **/
typedef void* (*thread_pset_allocate_t)(uint32_t tid, const void* data);

/**
 * @brief the callback function that is used when pointer needs to be disposed
 * @param mem the memory to be disposed
 * @param data the additional data passed in to the deallocator
 * @return status code
 **/
typedef int (*thread_pset_deallocate_t)(void* mem, const void* data);

/**
 * @brief the actual pointer array used to store the pointers
 **/
typedef struct _thread_pointer_array_t {
	struct _thread_pointer_array_t* unused; /*!< currently unused pointer array */
	uint32_t size;                          /*!< the size of the pointer */
	uintpad_t __padding__[0];
	void*    ptr[0];                        /*!< the actual pointers for each thread */
} thread_pointer_array_t;
STATIC_ASSERTION_LAST(thread_pointer_array_t, ptr);
STATIC_ASSERTION_SIZE(thread_pointer_array_t, ptr, 0);

/**
 * @brief the thread local pointer set
 * @details this is a implementation of a set of lock less pointers, each thread owns it's own pointer
 *          and it's able to automatically resize when the new thread is created
 **/
typedef struct _thread_pset_t {
	const void*              data;             /*!< The additional data passed to the allocator/deallocator */
	thread_pset_allocate_t   alloc;            /*!< The allocation function */
	thread_pset_deallocate_t dealloc;          /*!< The deallocation function */
	thread_pointer_array_t*  array;            /*!< The actual pointer array */
	pthread_mutex_t          resize_lock;      /*!< The resize lock for this pointer set */
	uint32_t                 is_alloc:1;       /*!< Indicates the thread pointer set is allocated dynamically */
} thread_pset_t;

#ifdef STACK_SIZE
/**
 * @brief Represent a thread stack
 **/
typedef struct {
	uint32_t  id;              /*!< The thread id */
	thread_type_t type;        /*!< the type of this thread */
	thread_t* thread;          /*!< The thread object */
	uintpad_t  __padding__[0];
	char base[0];             /*!< The base address of the stack */
} thread_stack_t;
#endif

/**
 * @brief create a new thread local pointer object
 * @param init_size the initial size of the pointer array
 * @param alloc the allocation callback
 * @param dealloc the deallocation callback
 * @param data the addtional data passed to the allocator/deallocator
 * @return the newly created object or NULL
 **/
#define thread_pset_new(init_size, alloc, dealloc, data, mem_args...) _thread_pset_new_impl(init_size, alloc, dealloc, data, ##mem_args, NULL)

thread_pset_t* _thread_pset_new_impl(uint32_t init_size, thread_pset_allocate_t alloc, thread_pset_deallocate_t dealloc, const void* data, ...)
    __attribute__((sentinel));

/**
 * @brief dispose a used thread local ptr
 * @return status code
 **/
int thread_pset_free(thread_pset_t* pset);

/**
 * @brief get the additional data that needs to be passed in to the callback
 * @param pset the pointer set
 * @return the data
 **/
const void* thread_pset_get_callback_data(thread_pset_t* pset);

/**
 * @brief get the ID for current thread
 * @return the result thread id
 **/
uint32_t thread_get_id(void);


/**
 * @brief create and start a new thread object
 * @param main the main function
 * @param data the additional user-defined thread data
 * @param type the type of thread
 * @return the newly created thread
 **/
thread_t* thread_new(thread_main_t main, void* data, thread_type_t type);

/**
 * @brief get current thread object
 * @return current thread object, if the thread is not created by thread_new, or there's an error return NULL
 **/
thread_t* thread_get_current(void);

/**
 * @brief set current thread name
 * @param name the name for the current thread
 * @return nothing
 **/
void thread_set_name(const char* name);

/**
 * @brief add a cleanup hook function to the thread
 * @param func the cleanup function
 * @param data the cleanup function's additional data
 * @return status code
 **/
int thread_add_cleanup_hook(thread_cleanup_t func, void* data);

/**
 * @brief send a signal to the thread
 * @param thread the thread object
 * @param signal the signal to send
 * @return status code
 **/
int thread_kill(thread_t* thread, int signal);

/**
 * @brief wait the thread terminates and then dispose the thread
 * @note this function will block until the thread terminates
 * @param thread the thread to dispose
 * @param ret the return value, NULL if we want to discard  the return value
 * @return status code
 **/
int thread_free(thread_t* thread, void** ret);

/**
 * @brief convert the thread name to the human readable string
 * @param type the thread type code
 * @param buf  the result buffer
 * @param size the size of the buffer
 * @return the result buffer or NULL on error case
 * @note the output format should be [type1,type2,type3]...
 **/
const char* thread_type_name(thread_type_t type, char* buf, size_t size);

/**
 * @brief Run the main function for testing, for more information see the
 *        documentation for thread_test_main_t
 * @param main the testing main function
 * @return exit code
 * @note This function does not require the entire system initialized
 **/
int thread_run_test_main(thread_test_main_t main);

#	ifdef STACK_SIZE
/**
 * @brief Get the current stack object
 * @return The pointer of current stack
 * @note This only works with the thread created by thread_new
 **/
static inline thread_stack_t* thread_get_current_stack(void)
{
	uintptr_t addr = (uintptr_t)&addr;
	addr = addr - addr % STACK_SIZE - sizeof(thread_stack_t);
	return (thread_stack_t*)addr;
}

__attribute__((noinline)) void* _thread_allocate_current_pointer(thread_pset_t* pset, uint32_t tid);

static inline void* thread_pset_acquire(thread_pset_t* pset)
{
	uint32_t tid = thread_get_current_stack()->id;

	thread_pointer_array_t* current = pset->array;

	/* If the pointer for the thread is already there */
	if(PREDICT_TRUE(current->size > tid))
	    return current->ptr[tid];

	return _thread_allocate_current_pointer(pset, tid);
}

static inline thread_type_t thread_get_current_type(void)
{
	return thread_get_current_stack()->type;
}

#	else
/**
 * @brief acquire the pointer for current thread from the thread local pointer set
 * @param pset the pointer set to acquire
 * @note if the pointer desn't exist for current thread, the alloc function will be called and
 *       will be assigned to this thread
 * @return the thread local pointer or NULL on error
 **/
void* thread_pset_acquire(thread_pset_t* pset);

/**
 * @brief get the current type of the thread
 * @return the thread type
 * @return the thread type or status code
 **/
thread_type_t thread_get_current_type(void);
#	endif

#	ifdef STACK_SIZE
/**
 * @brief Start the main function with an aligned stack
 * @param main The main function
 * @param argc The number of argument
 * @param argc The argument array
 * @return the exit code
 **/
int thread_start_with_aligned_stack(int (*main)(int argc, char** argv), int argc, char** argv);
#	endif
#endif
