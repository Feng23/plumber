/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <constants.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>

#ifdef __LINUX__
#include <sys/prctl.h>
#endif

#include <arch/arch.h>

#include <error.h>
#include <predict.h>
#include <utils/log.h>
#include <utils/thread.h>
#include <utils/string.h>
#include <utils/static_assertion.h>

/**
 * @brief the human readable thread type name
 **/
static const char* _type_name[] = {
	"EventLoopThread",
	"DispatcherThread",
	"WorkerThread",
	"AsyncIOThread",
	NULL
};

/**
 * @brief the represent a cleanup hook
 **/
typedef struct _cleanup_hook_t {
	thread_cleanup_t func;   /*!< the function */
	void*            arg;    /*!< the argument */
	struct _cleanup_hook_t* next;   /*!< the next callback function */
} _cleanup_hook_t;

/**
 * @brief the actual data structure for a thread object
 * @note the cleanup hook is actually a stack, the latest added function will be executed first
 **/
struct _thread_t {
	pthread_t     handle;      /*!< the pthread handle */
	thread_main_t main;        /*!< the thread main function */
#ifndef STACK_SIZE
	thread_type_t type;        /*!< the type of this thread */
#endif
	void*         arg;         /*!< the thread argument */
	_cleanup_hook_t* hooks;    /*!< the cleanup hooks */
#ifdef STACK_SIZE
	char             mem[STACK_SIZE * 2 + sizeof(thread_stack_t)]; /*!< The memory used for task */
	thread_stack_t*  stack;       /*!< The stack we need to use */
#endif
};

#ifndef STACK_SIZE
/**
 * @brief indicates which thread is it
 **/
static __thread uint32_t _thread_id = ERROR_CODE(uint32_t);

/**
 * @brief current thread object
 **/
static __thread thread_t* _thread_obj = NULL;
#endif

/**
 * @brief used to assign an untagged thread a thread id
 **/
static uint32_t _next_thread_id = 0;

/**
 * @brief get the thread id of current thread
 * @return the thread id
 **/
static inline uint32_t _get_thread_id(void)
{
#ifdef STACK_SIZE
	return thread_get_current_stack()->id;
#else
	if(PREDICT_FALSE(_thread_id == ERROR_CODE(uint32_t)))
	{
		uint32_t claimed_id;
		do {
			claimed_id = _next_thread_id;
		} while(!__sync_bool_compare_and_swap(&_next_thread_id, claimed_id, claimed_id + 1));

		_thread_id = claimed_id;

		LOG_DEBUG("Assign new thread ID %u to thread", claimed_id);
	}
	return _thread_id;
#endif
}

__attribute__((noinline))
/**
 * @brief Allocate the pointer for current thread
 * @note  Because GCC always wants to inline anything if possible on -O3,
 *        However, this inline is harmful, because the allocation will only
 *        happen limited times. So this inline causes the function needs to
 *        save more registers than it actally needs
 * @param pset The pointer set
 * @param tid  The thread id
 * @return pointer has been allocated
 **/
#ifndef STACK_SIZE
static
#endif
void* _thread_allocate_current_pointer(thread_pset_t* pset, uint32_t tid)
{
	if((errno = pthread_mutex_lock(&pset->resize_lock)) != 0)
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot acquire the resize lock");

	/* Then the thread should be the only one executing this code,
	 * At the same time, we need to check again, in case the pointer
	 * has been created during the time of the thread being blocked */
	thread_pointer_array_t* current = pset->array;
	if(current->size > tid)
	{
		if((errno = pthread_mutex_unlock(&pset->resize_lock)) != 0)
		    LOG_WARNING_ERRNO("Cannot release the resize lock");
		return current->ptr[tid];
	}

	/* Otherwise, it means we need to create more cells in the array */
	uint32_t new_size = current->size;
	for(;new_size <= tid; new_size *= 2);
	LOG_TRACE("Resizing the pointer array from size %u to %u", current->size, new_size);

	thread_pointer_array_t* new_array = (thread_pointer_array_t*)malloc(new_size * sizeof(void*) + sizeof(thread_pointer_array_t));
	uint32_t i = 1;
	if(NULL == new_array)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the new array");

	new_array->size = new_size;

	/*Then copy the previous pointers to new one */
	memcpy(new_array->ptr, current->ptr, current->size * sizeof(void*));

	/* Then allocate new pointers for the new threads */
	for(i = current->size; i < new_size; i ++)
	{
		if(NULL == (new_array->ptr[i] = pset->alloc(i, pset->data)))
		    ERROR_LOG_GOTO(ERR, "Cannot allocate memory for the new pointer");
	}

	new_array->unused = current;

	pset->array = new_array;

	if((errno = pthread_mutex_unlock(&pset->resize_lock)) != 0)
	    LOG_WARNING_ERRNO("Cannot release the resize lock");

	return pset->array->ptr[tid];
ERR:
	if((errno = pthread_mutex_unlock(&pset->resize_lock)) != 0)
	    LOG_WARNING_ERRNO("Cannot release the resize lock");

	if(new_array != NULL)
	{
		for(; i >= current->size; i --)
		    if(new_array->ptr[i] != NULL)
		        pset->dealloc(new_array->ptr[i], pset->data);
		free(new_array);
	}

	return NULL;
}

#ifndef STACK_SIZE
static inline void* _get_current_pointer(thread_pset_t* pset)
{
	uint32_t tid = _get_thread_id();

	thread_pointer_array_t* current = pset->array;

	/* If the pointer for the thread is already there */
	if(PREDICT_TRUE(current->size > tid))
	    return current->ptr[tid];

	return _thread_allocate_current_pointer(pset, tid);
}
#endif

thread_pset_t* _thread_pset_new_impl(uint32_t init_size, thread_pset_allocate_t alloc, thread_pset_deallocate_t dealloc, const void* data, ...)
{
	if(NULL == alloc || NULL == dealloc || init_size == 0 || init_size == ERROR_CODE(uint32_t))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	void* mem = NULL;

	va_list ap;
	va_start(ap, data);
	mem = va_arg(ap, void*);
	va_end(ap);

	uint32_t mutex_init = 0;
	uint32_t i = 0;
	thread_pset_t* ret = (thread_pset_t*)(mem == NULL ? malloc(sizeof(thread_pset_t)) : mem);
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the thread pointer set object");

	ret->is_alloc = (mem == NULL);
	ret->array = NULL;

	if((errno = pthread_mutex_init(&ret->resize_lock, NULL)) != 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot initialize the resize lock");

	ret->alloc = alloc;
	ret->dealloc = dealloc;

	if(NULL == (ret->array = (thread_pointer_array_t*)calloc(1, sizeof(thread_pointer_array_t) + sizeof(void*) * init_size)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the poitner array");

	for(i = 0; i < init_size; i ++)
	    if(NULL == (ret->array->ptr[i] = alloc(i, data)))
	        ERROR_LOG_GOTO(ERR, "Cannot create new pointer for thread %u", i);

	ret->array->size = init_size;
	ret->data = data;

	return ret;
ERR:
	if(mutex_init && (errno = pthread_mutex_unlock(&ret->resize_lock)) != 0)
	    LOG_WARNING_ERRNO("Cannot dispose the resize lock");
	if(NULL != ret->array)
	{
		uint32_t j;
		for(j = 0; j < i; j ++)
		    dealloc(ret->array->ptr[j], data);
		free(ret->array);
	}
	free(ret);
	return NULL;
}

int thread_pset_free(thread_pset_t* pset)
{
	if(NULL == pset) ERROR_RETURN_LOG(int, "Invalid arguments");

	int rc = 0;

	if((errno = pthread_mutex_destroy(&pset->resize_lock)) != 0)
	{
		LOG_WARNING_ERRNO("Cannot dispose the resize lock");
		rc = ERROR_CODE(int);
	}

	uint32_t i;
	for(i = 0; i < pset->array->size; i ++)
	    if(pset->dealloc(pset->array->ptr[i], pset->data) == ERROR_CODE(int))
	    {
		    LOG_WARNING("Cannot dispose the pointer for thread %u", i);
		    rc = ERROR_CODE(int);
	    }

	thread_pointer_array_t* ptr, *tmp;
	for(ptr = pset->array; ptr != NULL; )
	{
		tmp = ptr;
		ptr = ptr->unused;
		free(tmp);
	}

	if(pset->is_alloc) free(pset);

	return rc;
}
#ifndef STACK_SIZE
void* thread_pset_acquire(thread_pset_t* pset)
{
#ifndef FULL_OPTIMIZATION
	if(PREDICT_FALSE(NULL == pset))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");
#endif
	return _get_current_pointer(pset);
}
#endif

const void* thread_pset_get_callback_data(thread_pset_t* pset)
{
	if(NULL == pset) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return pset->data;
}

uint32_t thread_get_id()
{
	return _get_thread_id();
}

static void* _thread_main(void* data)
{
#ifdef STACK_SIZE
	thread_stack_t* stack = thread_get_current_stack();

	do {
		stack->id = _next_thread_id;
	} while(!__sync_bool_compare_and_swap(&_next_thread_id, stack->id, stack->id + 1));
#else
	_thread_obj = (thread_t*)data;
#endif

	thread_t* thread = (thread_t*)data;
	void* ret = thread->main(thread->arg);

	_cleanup_hook_t* ptr;
	for(ptr = thread->hooks; NULL != ptr; ptr = ptr->next)
	    if(ERROR_CODE(int) == ptr->func(thread->arg, ptr->arg))
	        LOG_WARNING("Thread cleanup function <func = %p, arg = %p> returned with an error", ptr->func, ptr->arg);

	return ret;
}

#ifdef STACK_SIZE
static void* _start_main(void *ctx)
{
	thread_get_current_stack()->id = 0;
	_next_thread_id = 1;
	thread_test_main_t func = (thread_test_main_t)ctx;;
	if(func() == 0) return ctx;
	return NULL;
}
#endif

int thread_run_test_main(thread_test_main_t func)
{
#ifdef STACK_SIZE
	thread_t* ret = (thread_t*)malloc(sizeof(thread_t));
	if(NULL == ret) return -1;

	uintptr_t offset = (STACK_SIZE - ((uintptr_t)ret->mem) % STACK_SIZE) % STACK_SIZE;
	if(offset >= sizeof(thread_stack_t))
	    ret->stack = (thread_stack_t*)(ret->mem + offset - sizeof(thread_stack_t));
	else
	    ret->stack = (thread_stack_t*)(ret->mem + offset + STACK_SIZE - sizeof(thread_stack_t));

	ret->stack->thread = NULL;
	ret->stack->type = THREAD_TYPE_GENERIC;

	pthread_attr_t attr;
	void* rc;
	if((errno = pthread_attr_init(&attr)) != 0)
	    goto ERR;

	if((errno = pthread_attr_setstack(&attr, ret->stack->base, STACK_SIZE)) != 0)
	    goto ERR;

	if((errno = pthread_create(&ret->handle, &attr, _start_main, func)) != 0)
	    goto ERR;

	if((errno = pthread_join(ret->handle, &rc)) != 0)
	    goto ERR;

	free(ret);

	if(NULL == rc) return -1;
	return 0;
ERR:
	free(ret);
	return -1;
#else
	return func();
#endif
}

thread_t* thread_new(thread_main_t main, void* data, thread_type_t type)
{
	if(NULL == main) ERROR_PTR_RETURN_LOG("Invalid arguments");

	if(type != THREAD_TYPE_GENERIC)
	{
		thread_type_t mask;
		for(mask = 1; mask != 0; mask *= 2)
		    if(type == mask) break;

		if(mask == 0) ERROR_PTR_RETURN_LOG("Invalid thread type");
	}

	thread_t* ret = (thread_t*)malloc(sizeof(thread_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the new thread");

	ret->main = main;
	ret->arg  = data;
	ret->hooks = NULL;
#ifdef STACK_SIZE
	uintptr_t offset = (STACK_SIZE - ((uintptr_t)ret->mem) % STACK_SIZE) % STACK_SIZE;
	if(offset >= sizeof(thread_stack_t))
	    ret->stack = (thread_stack_t*)(ret->mem + offset - sizeof(thread_stack_t));
	else
	    ret->stack = (thread_stack_t*)(ret->mem + offset + STACK_SIZE - sizeof(thread_stack_t));

	ret->stack->thread = ret;
	ret->stack->type = type;

	pthread_attr_t attr;
	if((errno = pthread_attr_init(&attr)) != 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create attribute of the thread");

	if((errno = pthread_attr_setstack(&attr, ret->stack->base, STACK_SIZE)) != 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set the base address of the stack");

	if((errno = pthread_create(&ret->handle, &attr, _thread_main, ret)) != 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot start the thread");
#else
	ret->type = type;
	if((errno = pthread_create(&ret->handle, NULL, _thread_main, ret)) != 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot start the thread");
#endif

	return ret;
ERR:
	if(NULL != ret) free(ret);
	return NULL;
}

thread_t* thread_get_current()
{
#ifdef STACK_SIZE
	return thread_get_current_stack()->thread;
#else
	return _thread_obj;
#endif
}

int thread_add_cleanup_hook(thread_cleanup_t func, void* data)
{
	if(NULL == func) ERROR_RETURN_LOG(int, "Invalid arguments");
	thread_t* thread = thread_get_current();

	if(NULL == thread) ERROR_RETURN_LOG(int, "Cannot add hook function to a thread not created by the thread_new function");

	_cleanup_hook_t* hook = (_cleanup_hook_t*)malloc(sizeof(_cleanup_hook_t));
	if(NULL == hook) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the cleanup hook object");

	hook->func = func;
	hook->arg  = data;
	hook->next = thread->hooks;

	thread->hooks = hook;

	LOG_DEBUG("Hook function @ <func = %p, data = %p> has been registered", func, data);
	return 0;
}

int thread_kill(thread_t* thread, int signal)
{
	if(NULL == thread) ERROR_RETURN_LOG(int, "Invalid arguments");

	if((errno = pthread_kill(thread->handle, signal)) != 0 && errno != ESRCH)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot send signal to target thread");

	return 0;
}

int thread_free(thread_t* thread, void** ret)
{
	if(NULL == thread) ERROR_RETURN_LOG(int, "Invalid arguments");

	if((errno = pthread_join(thread->handle, ret)) != 0)
	{
		free(thread);
		ERROR_RETURN_LOG(int, "Cannot join the thread");
	}

	_cleanup_hook_t* ptr, *tmp;
	for(ptr = thread->hooks; NULL != ptr;)
	{
		tmp = ptr;
		ptr = ptr->next;
		free(tmp);
	}

	free(thread);
	return 0;
}

void thread_set_name(const char* name)
{
#ifdef __LINUX__
	prctl(PR_SET_NAME, name, 0, 0, 0);
#else
	(void)name;
#endif
}

#ifndef STACK_SIZE
thread_type_t thread_get_current_type()
{
	const thread_t* thread = thread_get_current();

	return NULL != thread ? thread->type : THREAD_TYPE_GENERIC;
}
#endif

const char* thread_type_name(thread_type_t type, char* buf, size_t size)
{
	if(NULL == buf)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	string_buffer_t sbuf;

	string_buffer_open(buf, size, &sbuf);

	thread_type_t mask;
	uint32_t i, first = 1;
	for(mask = 1, i = 0; mask < THREAD_TYPE_MAX && NULL != _type_name[i]; mask <<= 1, i ++)
	    if(type & mask)
	    {
		    if(first) string_buffer_appendf(&sbuf, "[%s", _type_name[i]);
		    else      string_buffer_appendf(&sbuf, ",%s", _type_name[i]);
		    first = 0;
	    }

	string_buffer_append("]", &sbuf);

	return string_buffer_close(&sbuf);
}

#ifdef STACK_SIZE
int thread_start_with_aligned_stack(int (*main)(int argc, char** argv), int argc, char** argv)
{
	static thread_t th = {};

	uintptr_t offset = (STACK_SIZE - ((uintptr_t)th.mem) % STACK_SIZE) % STACK_SIZE;
	if(offset >= sizeof(thread_stack_t))
	    th.stack = (thread_stack_t*)(th.mem + offset - sizeof(thread_stack_t));
	else
	    th.stack = (thread_stack_t*)(th.mem + offset + STACK_SIZE - sizeof(thread_stack_t));

	th.stack->thread = &th;
	th.stack->type = THREAD_TYPE_GENERIC;

	/* We need assign a thread id to the main thread once we switch the stack */
	do {
		th.stack->id = _next_thread_id;
	} while(!__sync_bool_compare_and_swap(&_next_thread_id, th.stack->id, th.stack->id + 1));


	return arch_switch_stack(th.stack->base, STACK_SIZE, main, argc, argv);
}
#endif
