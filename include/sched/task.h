/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the scheduler task is a servlet task with service related information
 * @details this part is used to manage the pending tasks, which is the task has been constructed
 *          but it's not ready to goes into the queue. All the task is associated with a request ID
 *          which is the identifier to each input of the input node
 * @file sched/task.h
 **/
#ifndef __PLUMBER_SCHED_TASK_H__
#define __PLUMBER_SCHED_TASK_H__

/**
 * @brief The scheduler task context (STC)
 * @note  This is the scheduler's task context, for each scheduler thread we have
 *        a isolated scheduler task context, which include a task table, a read
 *        task queue and metadata. <br/>
 *        Before we introduces this, the context is implemented by a group of thread
 *        locals, however, the performance penalty is so high. So that we introduced
 *        this context
 **/
typedef struct _sched_task_context_t sched_task_context_t;

/**
 * @brief the request identifier
 **/
typedef uint64_t sched_task_request_t;

/**
 * @brief the type for a scheduler task
 **/
typedef struct _sched_task_t sched_task_t;

/**
 * @brief The actual type for a scheduler task
 **/
struct _sched_task_t {
	sched_task_context_t*    ctx;     /*!< The scheduler task context */
	const sched_service_t*   service; /*!< the service that owns this task */
	sched_rscope_t*          scope;   /*!< the request local scope for this task */
	sched_service_node_id_t  node;    /*!< the node id for this task */
	sched_task_request_t     request; /*!< the request id for this task */
	runtime_task_t*          exec_task; /*!< the actual runtime task, for an async task, this is the async init task<br/>
	                                     *   And we are able to get related task based on this */
};

/**
 * @brief initialize the scheduler task table
 * @return status code
 **/
int sched_task_init(void);

/**
 * @brief finalize the scheduler task table
 * @note the main thread will call this function in the end, so the main thread do not need to call this
 * @return status code
 **/
int sched_task_finalize(void);

/**
 * @brief Initialize a new scheduler task context
 * @param thread_ctx The thread context that creates this scheduler context
 * @return status code
 **/
sched_task_context_t* sched_task_context_new(sched_loop_t* thread_ctx);

/**
 * @brief Dispose a used scheduler task context
 * @return status code
 **/
int sched_task_context_free(sched_task_context_t* ctx);

/**
 * @brief notify the task table there's an incoming request
 * @param ctx The scheduler task context
 * @param service the target service
 * @param input_pipe the input of this request
 * @param output_pipe the output for this request
 * @return the request identifier or negative status code
 **/
sched_task_request_t sched_task_new_request(sched_task_context_t* ctx, const sched_service_t* service, itc_module_pipe_t* input_pipe, itc_module_pipe_t* output_pipe);

/**
 * @brief notify the task table there's a newly create pipe which can connect to the given node, given pipe
 *        if there's no task for this request on this node, create a new task
 *        It will return a ready task if this turns out that one task becomes mature
 * @note Since now we have the async task, which means we can not mark the downstream task is ready before the async
 *       task is done. <br/>
 *       This means, we need to sperate this function into two stage: <br/>
 *       First, we need to assign the downstream runtime task pipes array <br/>
 *       Second, we need to notify the downstream task for the pipe ready state <br/>
 *       For the sync task, the logic will be the same. However, for the async one,
 *       we need do the first step before the async_setup task is called and do the second
 *       step when the async_cleanup is completed. <br/>
 *       This function actually handle this, if async = 0, the behavior is the original one, which
 *       do the step 1 and step 2 at the same time.
 *       if the async = 1, and handle is not NULL, we actually do stage 1 <br/>
 *       if the async = 1 and handle is NULL, we do stage 2 <br/>
 * @param service the target service
 * @param ctx The scheduler task context
 * @param node the target node
 * @param request the request ID
 * @param pipe the target pipe of the node
 * @param handle the pipe handle
 * @param async if this is an async task, which means we can not set the downstream end of the pipe to ready right away
 * @return status code
 **/
int sched_task_input_pipe(sched_task_context_t* ctx, const sched_service_t* service, sched_task_request_t request,
                          sched_service_node_id_t node, runtime_api_pipe_id_t pipe,
                          itc_module_pipe_t* handle, int async);

/**
 * @brief get next runnable task, and remove the task from the list
 * @note  the caller should create all the output pipes before actually launch the task
 * @param ctx The scheduler task context
 * @return the ready task or NULL when error happens
 **/
sched_task_t* sched_task_next_ready_task(sched_task_context_t* ctx);

/**
 * @brief Notify the task scheduler on the async task compeletion event, this means we should
 *        move on to the downstream task in this request
 * @param task The async task we want to nofiy
 * @return status code
 **/
int sched_task_async_completed(sched_task_t* task);

/**
 * @brief dispose a task that is already launched
 * @param task the task to dispose
 * @return status code
 **/
int sched_task_free(sched_task_t* task);

/**
 * @brief notify that this task has a pipe gets ready
 * @param task the task to notify
 * @return status code
 **/
int sched_task_pipe_ready(sched_task_t* task);

/**
 * @brief notify that there's an new output pipe will be connect to the service node.
 *        The interface has a sched_task_t pointer, because we can only do this if the service is
 *        ready to run
 * @param task the target task
 * @param pipe the target pipe id
 * @param handle the pipe handle to set
 * @return status code
 **/
int sched_task_output_pipe(sched_task_t* task, runtime_api_pipe_id_t pipe, itc_module_pipe_t* handle);

/**
 * @brief notify that there's a new output shadow pipe will be connected to the service node
 *        It's similar to its normal pipe version, sched_task_output_pipe. <br/>
 *        But the only difference is, it actually pass in the forked pipe handle and
 *        do not claim the ownership of the pipe
 * @note the pipe MUST have a pipe flag of PIPE_SHADOW | PIPE_INPUT, which indicates the pipe is forked
 *       and it's the input side rather than the normal output pipe. Otherwise it will return an error code
 * @param task the target task
 * @param pipe the target pipe id
 * @param handle the pipe handle
 * @return status code
 **/
int sched_task_output_shadow(sched_task_t* task, runtime_api_pipe_id_t pipe, itc_module_pipe_t* handle);

/**
 * @brief notify that the task has an input pipe cancelled. If all its inputs are cancled, the task will
 *        be cancelled and all the downstream pipes are cancelled
 * @note  this does not change the ready state, so another pipe_ready call is reuiqred to get the task in ready state
 * @param task the target task
 * @return status code
 **/
int sched_task_input_cancelled(sched_task_t* task);

/**
 * @brief check the status of the request, which is either  working on that or not
 * @note this function is only used for testing
 * @param request the request id
 * @param ctx The scheduler task context
 * @return if the scheduler is currently working on this (or in the pending state), or error code
 **/
int sched_task_request_status(const sched_task_context_t* ctx, sched_task_request_t request);

/**
 * @brief Launch the async task
 * @param task The task we need to launch, what it actually did is post the async task
 *       to the async task queue
 * @return status code
 **/
int sched_task_launch_async(sched_task_t* task);

/**
 * @brief Get the number of running requests
 * @param ctx The context
 * @return status code
 **/
uint32_t sched_task_num_concurrent_requests(const sched_task_context_t* ctx);

#endif /* __PLUMBER_SCHED_TASK_H__ */
