/**
 * Copyright (C) 2017-2018, Hao Hou
 **/

/**
 * @file runtime/api.h
 * @brief define the exported type that can be used by the pservlet code
 * @note this file export the data to servlet
 **/
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <utils/static_assertion.h>

#ifndef __PLUMBER_RUNTIME_API_H__
#define __PLUMBER_RUNTIME_API_H__

/** @brief the type used to the pipe ID */
typedef uint16_t runtime_api_pipe_id_t;

/**
 * @brief the type used to represent a pipe object, either a real pipe or a reference
 *        to a module function
 * @note there are two different memory layout for this type <br/>
 *       a) 11111111 00000000 pppppppp pppppppp <br/>
 *          This is used when we refer a pipe id <br/>
 *       b) mmmmmmmm oooooooo oooooooo oooooooo <br/>
 *          This is used when we refer a service module function
 */
typedef uint32_t runtime_api_pipe_t;

/**
 * @brief check if the pipe is a virtual pipe
 * @param pipe the pipe to check
 **/
#define RUNTIME_API_PIPE_IS_VIRTUAL(pipe) (((pipe) & 0xff000000u) != 0xff000000u)

/**
 * @brief check if this pipe is a normal pipe
 * @param pipe the pipe to check
 **/
#define RUNTIME_API_PIPE_IS_NORMAL(pipe) (((pipe) & 0xff000000u) == 0xff000000u)

/**
 * @brief convert a pipe id to pipe_t
 * @param pid the pipe id
 * @return the result pipe_t
 **/
#define RUNTIME_API_PIPE_FROM_ID(pid) ((runtime_api_pipe_t)(0xff000000u | (pid)))

/**
 * @brief convert a pipe_t to pid
 * @param pipe the pipe_t
 * @return the convert result
 **/
#define RUNTIME_API_PIPE_TO_PID(pipe) ((runtime_api_pipe_id_t)((pipe)&0xffffffu))

/**
 * @brief get the module part of a virtual pipe
 * @param pipe the pipe_t
 * @return the mdoule type code
 **/
#define RUNTIME_API_PIPE_VIRTUAL_GET_MODULE(pipe) ((pipe) >> 24)

/**
 * @brief get the opcode part of a virtual pipe
 * @param pipe the pipe_t
 * @return the module service opcode
 **/
#define RUNTIME_API_PIPE_VIRTUAL_GET_OPCODE(pipe) ((pipe)&0xffffffu)

/** @brief the type used for the pipe define flags
 *  @note the bit layout of a pipe flags is  <br/>
 *        rrrrrrrr rrrDsapd tttttttt tttttttt <br/>
 *        D = disabled <br/>
 *        s = Shadow Pipe <br/>
 *        a = Async Pipe <br/>
 *        r = Reserved <br/>
 *        p = Presist  <br/>
 *        d = Pipe direction <br/>
 *        t = Target Pipe <br/>
 **/
typedef uint32_t runtime_api_pipe_flags_t;
/** @brief the target pipe mask */
#define RUNTIME_API_PIPE_FLAGS_TARGET_MASK ((runtime_api_pipe_flags_t)0xffffu)

/** @brief the flag which indicates that this pipe is a input pipe */
#define RUNTIME_API_PIPE_INPUT   ((runtime_api_pipe_flags_t)0x00000u)
/** @brief the flag which indicates that this pipe is a output pipe */
#define RUNTIME_API_PIPE_OUTPUT  ((runtime_api_pipe_flags_t)0x10000u)
/**
 *  @brief Mark the pipe as persistent
 *  @note This flag *suggests* (not requires) the pipe module
 *  hold the resources this pipe occupied even if a purge is required
 *  because it will be reused later. <br/>
 *  This is bascially an abstraction of persist TCP connection. But also
 *  can be use in other types of pipe.<br/>
 *  If the module do not support this "persistency" (such as mempipe)
 *  it can simple ignore the flag
 **/
#define RUNTIME_API_PIPE_PERSIST ((runtime_api_pipe_flags_t)0x20000u)

/** @brief only used in output pipes, if this bit is set, the pipe_write function will be in async mode */
#define RUNTIME_API_PIPE_ASYNC ((runtime_api_pipe_flags_t)0x40000u)

/** @brief only used in output pipes, this flag means the pipe is a "shadow" of another pipe, which means the content
 *         in this pipe should be identical to another */
#define RUNTIME_API_PIPE_SHADOW ((runtime_api_pipe_flags_t)0x80000u)

/**
 * @brief Used only for the shadow pipes <br/>
 *        if this flag is set, the pipe will be disabled by default, which means,
 * 	      For shadow pipe, the downstream will be cancelled by default, even if the target pipe is not cancelled <br/>
 *        This is useful on shadow pipes <br/>
 *        Suppose we are implementing a conditional servlet, which feed data to
 *        only one output and cancel all others. <br/>
 *        We can implement this with the shadow pipe with disabled flags, and
 *        the pipe seletected will have the disabled flag removed. <br/>
 *        In this way, there's no actual data copy happened
 **/
#define RUNTIME_API_PIPE_DISABLED ((runtime_api_pipe_flags_t)0x100000u)

/** @brief check the pipe is a read end */
#define RUNTIME_API_PIPE_IS_INPUT(f) (!RUNTIME_API_PIPE_IS_OUTPUT(f))

/** @brief check the pipe is a write end */
#define RUNTIME_API_PIPE_IS_OUTPUT(f) (((f) & RUNTIME_API_PIPE_OUTPUT) == RUNTIME_API_PIPE_OUTPUT)

/** @brief get the target pipe from the pipe flags */
#define RUNTIME_API_PIPE_GET_TARGET(flags) ((flags) & RUNTIME_API_PIPE_FLAGS_TARGET_MASK)

STATIC_ASSERTION_EQ_ID(RUNTIME_API_PIPE_INPUT_CHECK_POS, RUNTIME_API_PIPE_IS_INPUT(RUNTIME_API_PIPE_INPUT),  1);
STATIC_ASSERTION_EQ_ID(RUNTIME_API_PIPE_INPUT_CHECK_NEG, RUNTIME_API_PIPE_IS_INPUT(RUNTIME_API_PIPE_OUTPUT), 0);
STATIC_ASSERTION_EQ_ID(RUNTIME_API_PIPE_OUTPUT_CHECK_POS, RUNTIME_API_PIPE_IS_OUTPUT(RUNTIME_API_PIPE_INPUT), 0);
STATIC_ASSERTION_EQ_ID(RUNTIME_API_PIPE_OUTPUT_CHECK_NEG, RUNTIME_API_PIPE_IS_OUTPUT(RUNTIME_API_PIPE_OUTPUT),1);

/**
 * @brief the mask bits for the shared flags
 **/
#define RUNTIME_API_PIPE_SHARED_MASK (\
        RUNTIME_API_PIPE_PERSIST\
)

/**
 * @brief get the flags that should be shared accross all companions
 * @param f the flags
 **/
#define RUNTIME_API_PIPE_GET_SHARED_FLAGS(f) ((f) & RUNTIME_API_PIPE_SHARED_MASK)

STATIC_ASSERTION_EQ_ID(flags_dir1, 0, (RUNTIME_API_PIPE_GET_SHARED_FLAGS((runtime_api_pipe_flags_t)-1)) & RUNTIME_API_PIPE_INPUT);
STATIC_ASSERTION_EQ_ID(flags_dir2, 0, (RUNTIME_API_PIPE_GET_SHARED_FLAGS((runtime_api_pipe_flags_t)-1)) & RUNTIME_API_PIPE_OUTPUT);
STATIC_ASSERTION_EQ_ID(flags_persist, RUNTIME_API_PIPE_PERSIST, (RUNTIME_API_PIPE_GET_SHARED_FLAGS((runtime_api_pipe_flags_t)-1)) & RUNTIME_API_PIPE_PERSIST);


/**
 * @brief the pipe cntl opcode for get the pipe flags
 * @note the bit layout of the pipe <br/>
 *       mmmmmmmm oooooooo oooooooo oooooooo <br/>
 *       m = Module ID <br/>
 *       o = Opcode    <br/>
 *       for general operations, use 0xff as module id
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_GET_FLAGS 0xff000000u
/**
 * @brief set a flag bit
 * @note example: pipe_cntl(pipe, PIPE_CNTL_GET_FLAFS, &flag)
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_SET_FLAG  0xff000001u
/**
 * @brief Clear the flag bits
 * @note example: pipe_cntl(pipe, PIPE_CNTL_CLR_FLAGS, PIPE_CLOSED)
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_CLR_FLAG  0xff000002u

/**
 * @brief notify the module it reach the end of message token in the last buffer has read
 * @note example: pipe_cntl(pipe, PIPE_CNTL_EOM, eom_token_offset)
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_EOM       0xff000003u

/**
 * @brief push a state variable and attach it to the corresponding resource
 * @note example: pipe_cntl(pipe, PIPE_CNTL_PUSH_STATE, state, cleanup_func)
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_PUSH_STATE 0xff000004u

/**
 * @brief pop a previously pushed state and dettach it from the corresponding resource
 * @note example: pipe_cntl(pipe, PIPE_CNTL_POP_STATE, &state_buffer)
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_POP_STATE  0xff000005u

/**
 * @brief invoke a service module reference pipe
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_INVOKE     0xff000006u

/**
 * @brief read the typed header from the input pipe
 * @note example: pipe_cntl(pipe, PIPE_CNTL_READHDR, &hdr_buf, sizeof(hdr_buf), &size_buf) <br/>
 *       This function can be called multiple times until the typed header gets fully consumed. <br/>
 *       If we want to skip all the header section, we can call the read API directly
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_READHDR    0xff000007u

/**
 * @brief write the typed hearder to the output pipe
 * @note  example: pipe_cntl(pipe, PIPE_CNTL_WRITEHDR, &hdr_buf, sizeof(hdr_buf), &size_buf) <br/>
 *        Like the readhdr call, this function also have a stream model, please note
 *        the function will accept bytes larger than the expected header, and in this case
 *        the extra data will be silently dropped. <br/>
 *        If the pipe data body have been touched, the header data will be automatically fill zero
 *        If it's not previously written
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_WRITEHDR   0xff000008u

/**
 * @brief Get the underlying module path used by current pipe
 * @note  example: pipe_cntl(pipe, PIPE_CNTL_MODPATH, pathbuf, bufsize);
 *        this function will return the path string of the module the pipe is using
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_MODPATH   0xff000009u

/**
 * @brief Get the header buffer directly from the underlying module
 * @note  This function is similar to mmap in UNIX operating system. It actually
 *        returns a memory address that contains expected data (which can be the
 *        internal buffer or mmapped memory) directly. In this way, we are able
 *        to avoid the memcpy during the time of reading. But this makes the pipe
 *        can not dispose the buffer until the pipe handle is completed disposed.
 *        Since once we expose the address of internal buffer to the application
 *        space code, the buffer needs to be accessible during the entire life
 *        time of the handle.
 *        example: pipe_cntl(pipe, PIPE_CNTL_GET_HDR_BUF, nbytes, &hdr_buf);
 *
 *        It's possible that the header isn't carried completely by a continous memory
 *        region, in this case, the invocation might result empty. In this situation,
 *        the caller should use READHDR operation instead.
 *
 *        Note: there's no guareentee that the call will successfully pull out the buffer.
 *              It may fail for no reason or simplely because the module doesn't support
 *              this inerface, or even because the header is too large for a continuous
 *              memor region. In any case, we should have READHDR as "backup plan".
 *
 *        The hdr_buf = NULL indicates that we don't successfully pull out the expected buffer.
 *        Otherwise the returned buffer should contain at least nbytes of data to consume.
 *
 *        In this case, the handle's read pointer will be moved forward.
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_GET_HDR_BUF 0xff00000au

/**
 * @brief Get the data body buffer directly from the underlying module
 * @note  example: pipe_cntl(pipe, PIPE_CNLT_GET_DATA_BUF, required, &buffer, &upper_bound, &lower_bound);
 *        This function is similar to mmap in UNIX, which directly returns the memory region
 *        that contains the expected data. This call also doesn't guareentee successfully pulled
 *        out. And once we did this the exposed internal buffer should not be disposed until the
 *        pipe handle is dead.
 *        Since it's possible that the handle actually have no idea about when this request body
 *        ends (For example a HTTP request, or a line based text file). Thus we can not always
 *        get the correct data length. Instead it returns the upper bound of the availble size.
 *        Thus, accessing the memory beyond the boundary should be definitely an error.
 *
 *        When the module doesn't know how long the region is the lower_bould will be 0, otherwise
 *        the upper_bound and lower_bound are the same.
 *
 *        At anytime the memory region will be at most required bytes in size.
 *
 *        Once the module returns the internal buffer that have undetermined size, any other read
 *        operation (including the get data buf operation) will return 0, until the end of the
 *        memory region has been determined by the applicatoin code by PUT_DATA_BUF cntl operation
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_GET_DATA_BUF 0xff00000bu

/**
 * @brief Release the data body buffer which we get from get_data_buf
 * @note  example: pipe_cntl(pipe, PIPE_CNLT_PUT_DATA_BUF, buffer, size)
 *        This will inform the module about the actual size of the data that contains in the
 *        memory region. This is useful when we are dealing with line based text file.
 *        Since the buffer may contains the data from other lines, thus we can not determine
 *        the end point of the buffer. This give us a chance to inform the end of the data
 *        for this request in buffer without actuall scanning it.
 *
 *        This indicates the handle is currently has an undermined memory region result has
 *        been pulled out by the application layer code. Otherwise the call will be ignored
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_PUT_DATA_BUF 0xff00000cu

/**
 * @brief the pipe cntl opcode that indicates no operation
 * @note this is used when the mod_prefix doesn't return any prefix, we need use NOP as the returned opcode <br/>
 *       this should be different with the error code, because they are two different things, ERROR_CODE means program error
 *       But in this case, it just means ignore it
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_NOP        0xfffffffeu


/**
 * @brief get the module ID from a pipe_cntl opcode
 * @note op the op code
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_MODULE_ID(op) ((op) >> 24)

/**
 * @brief get the module specified op code
 * @note this will strip the module identifier part and only keep the module specified opcode
 **/
#define RUNTIME_API_PIPE_CNTL_OPCODE_MOD_SPEC(op) ((op) & 0x00fffffful)

/**
 * @brief make a module specified pipe control opcode
 * @param id the module id
 * @param op the module specified id
 **/
#define RUNTIME_API_PIPE_CNTL_MOD_OPCODE(id, op) (((id) << 24) | (0x00fffffful & op))

STATIC_ASSERTION_EQ_ID(__non_module_related_get_flags__, 0xff, RUNTIME_API_PIPE_CNTL_OPCODE_MODULE_ID(RUNTIME_API_PIPE_CNTL_OPCODE_GET_FLAGS));
STATIC_ASSERTION_EQ_ID(__non_module_related_set_flag__, 0xff, RUNTIME_API_PIPE_CNTL_OPCODE_MODULE_ID(RUNTIME_API_PIPE_CNTL_OPCODE_SET_FLAG));
STATIC_ASSERTION_EQ_ID(__non_module_related_clr_flag__, 0xff, RUNTIME_API_PIPE_CNTL_OPCODE_MODULE_ID(RUNTIME_API_PIPE_CNTL_OPCODE_CLR_FLAG));
STATIC_ASSERTION_EQ_ID(__non_module_related_eom__, 0xff, RUNTIME_API_PIPE_CNTL_OPCODE_MODULE_ID(RUNTIME_API_PIPE_CNTL_OPCODE_EOM));
STATIC_ASSERTION_EQ_ID(__non_module_related_push_state__, 0xff, RUNTIME_API_PIPE_CNTL_OPCODE_MODULE_ID(RUNTIME_API_PIPE_CNTL_OPCODE_PUSH_STATE));
STATIC_ASSERTION_EQ_ID(__non_module_related_pop_state__, 0xff, RUNTIME_API_PIPE_CNTL_OPCODE_MODULE_ID(RUNTIME_API_PIPE_CNTL_OPCODE_POP_STATE));

/**
 * @brief Switch the async task to the event mode, which means the async task processor won't
 *        emit the async task completed event until the given async task token has been notified
 *        finished
 * @example   ....
 * 			  async_cntl(ASYNC_CNTL_OPCODE_SET_WAIT, &task_handle);
 * 			  // Here we initialize a async IO and set a callback function on_finished
 * 			  return 0;
 * 			onfinished(void) {
 * 				// Post process the IO
 * 				async_cntl(ASYNC_CNTL_OPCODE_NOTIFY_WAIT, task_handle, status_code);
 * 				return 0;
 * 			}
 * @note In this example, we actually makes the async worker thread initialize an IO operation and yeild the async thread. <br/>
 *       When the operation is done, the callback function may or may not be called from the same thread. But if calls the notify
 *       operation, and this will makes the async task change it state from waiting to finished, at this point, the async task
 *       processor will emit the event to the event queue. <br/>
 *       If we are not switching to this mode, the async task event will be emitted right after the async callback function returns. <br/>
 *       The wait mode is an abstraction of the event driven IO model in the plumber framework. And it's more like to have high troughput
 *       The non-wait mode is the example for how we avoid the worker thread been blocked by the slow operation, like database access, etc.
 **/
#define RUNTIME_API_ASYNC_CNTL_OPCODE_SET_WAIT 0

/**
 * @brief Notify the waiting async task for the completed state. When this function is called, it means we have a async task has been set to
 *        the wait mode and we have completed the operation and it's the time for us to notify the framework on this
 * @note The usagage async_cntl(handle, ASYNC_CNTL_OPCODE_NOTIFY_WAIT, task_handle, status_code);
 **/
#define RUNTIME_API_ASYNC_CNTL_OPCODE_NOTIFY_WAIT 1

/**
 * @brief Gets the return status code of this task
 * @details This is useful when have the cleanup function, because in some case
 *          we need to know if this task is on error or not.
 *          This function give us a chance to get the return code of the asnyc task
 * @note usage async_cntl(handle, ASYNC_CNTL_OPCODE_RETCODE, &retcode);
 **/
#define RUNTIME_API_ASYNC_CNTL_OPCODE_RETCODE 2

/**
 * @brief Cancel the async task, which means we skip the async_exec task and move foward to the async_cleanup directly
 * @details This is useful when the servlet calls a server and maintains a cache. <br/>
 *         The async_init task can lookup the local cache first to determine if we need to actually make the remote call
 *         Also, we give this operation the ability to set the async operation's status code. Which means if the init task
 *         realize the cache has something wrong with it, it can skip the task and set the async task to error
 * @note usage async_cntl(handle, ASYNC_CNTL_OPCODE_CANCEL, status_code);
 **/
#define RUNTIME_API_ASYNC_CNTL_OPCODE_CANCEL 3

/**
 * @brief the token used to request local scope token
 **/
typedef uint32_t runtime_api_scope_token_t;

/**
 * @brief The event description of the event driven scope stream. Which is actually a file descriptor and
 *        when the file descriptor should be treated as the stream gets ready
 *        If both read and write is 0, it means we want to remove the event from the module
 **/
typedef struct {
	int       fd;     /*!< The FD that is used for event notification */
	uint32_t  read:1; /*!< Is the ready-for-read event a read event */
	uint32_t  write:1;/*!< Is the ready-for-write event a write event */
	int32_t   timeout; /*!< The time limit for the RLS token not gets ready */
} runtime_api_scope_ready_event_t;

/**
 * @brief Represent an entity in the scope. It's actually a group of callback function for the opeartion
 *        that is supported by the scope entity and a memory address which represent the entity data
 **/
typedef struct {

	void*                        data;         /*!< the actual pointer */

	/**
	 * @brief the callback function used to copy the memory
	 * @param ptr the pointer to copy
	 * @return the copied pointer
	 **/
	void* (*copy_func)(const void* ptr);

	/**
	 * @brief the callback used to dispose a managed pointer
	 * @note this is the only required callback for each RLS object
	 * @param ptr the pointer to dispose
	 * @return status code
	 **/
	int (*free_func)(void* ptr);

	/**
	 * @brief the callback function used to open the rscope pointer as a byte stream
	 * @note this is the callback function is used to serialize the RLS memory info a
	 *       byte stream. This feature is used for the framework to write a serialized
	 *       RLS directly to the pipe, which means we do not needs high level user-space
	 *       program to handle this. <br/>
	 *       The reason for why we have this is, without this mechanism,
	 *       when we build a file server, we need to read the file content into a mem pipe,
	 *       and then copy the mem pipe to async buffer and then write it to the socket.
	 *       Most of the operations are unncessary. By introducing the file RLS, and this
	 *       byte stream interface, we will be able to read the file from the async write loop
	 *       directly. In this way, we can elimite the memory copy completely. <br/>
	 * @param ptr the RLS pointer to open
	 * @return the byte stream handle, which is the state variable for the serialization, NULL on error case
	 **/
	void* (*open_func)(const void* ptr);

	/**
	 * @brief read bytes from the byte stream representation of the RLS pointer
	 * @param handle the byte stream handle
	 * @param buffer the buffer to return the read result
	 * @param bufsize the buffer size
	 * @return the bytes has been read to buffer, or error code
	 * @note If this function returns 0, it may indicates the stream is waiting for resource gets ready
	 *       In this case, if the user supports event driven interface, it may call event_func for the
	 *       event description that hints the availibility of the stream.
	 **/
	size_t (*read_func)(void* __restrict handle, void* __restrict buffer, size_t bufsize);

	/**
	 * @brief check if the byte stream representation of the RLS pointer has reached the end of stream (EOS)
	 * @param handle the byte stream handle
	 * @return the check result, 1 for true, 0 for false, error code on error cases
	 **/
	int (*eos_func)(const void* handle);

	/**
	 * @brief Get the event that should be used as the notification for the readiness of the stream
	 * @param handle The stream handle
	 * @param event_buf The buffer used to return the event
	 * @return Number of event has been registered, 0 if no event should be registered, 1 for needs to register one
	 *         event and error code for all the error cases
	 **/
	int (*event_func)(void* __restrict handle, runtime_api_scope_ready_event_t* event_buf);

	/**
	 * @brief close a used stream handle
	 * @param handle the handle to close
	 * @note this function will dispose the memory occupied by the handle
	 * @return status code
	 **/
	 int (*close_func)(void* handle);
} runtime_api_scope_entity_t;

/**
 * @brief describe the param for the request that ask for the write_token API consume the
 *        token data in the way specified by this
 * @details Problem: This mechanism is used to address the problem that the directly RLS token access interface
 *         is not buffer friendly. Consider we use an BIO object to write the pipe, so all the data that has written
 *         to pipe via BIO is bufferred in the BIO buffer. After this, if we want to write a RLS token, the BIO buffer
 *         has to flush no matter if it's full or not. <br/>
 *         Because the write_token call do not aware of the BIO buffer, so it will write the data directly, however all
 *         the bufferred BIO data which should be written before the token is now after the token content. <br/>
 *         However, this additional flush causes serious problem. Because we have to flush the buffer once the write_token has
 *         been called. For example, previously, we will be able to use the bio call like:
 *         <code>
 *         		pstd_bio_printf("<div>%s</div>", file_content);
 *         </code>
 *         To write the response which will translate to 1 pipe_write of course, however, by introducing the DRA, we should use:
 *         <code>
 *         		pstd_bio_printf("<div>");
 *         		pstd_bio_write_token(file_token);
 *         		pstd_bio_printf("</div>");
 *         </code>
 *         Which actually needs to translate to 3 pipe_write, which turns out to be 3 syscalls. <br/>
 *         This causes a huge performance downgrade from 115K Req/sec to 68K req/sec for the user-agent echo back server.
 *         Solution: This is the descriptor that request the first N bytes from the RLS token stream, and this data will be passed in
 *         to the callback provided by the caller of write_token, by having this callback, the caller (which is PSTD BIO of course),
 *         will have a last chance to fill the unused buffer. If the stream is exhuasted by the data request, no underlying DRA will
 *         happen, otherwise, the DRA will handle the remaining portion of the stream. <br/>
 *         If the data is exhuasted by the data request and BIO buffer is not full, then the buffer won't flush. In this way, we make
 *         a full use of user-space buffer before we flush it.
 **/
typedef struct {
	size_t  size;     /*!< The number of bytes we are reqeusting */
	void*   context;  /*!< The caller context of this data request */
	/**
	 * @brief the callback function that handles the requested data, it may be called multiple times
	 *        once the data_handler returns 0, then it means the data request do not want the data anymore
	 * @param context the caller defined context
	 * @param data the pointer to the data section
	 * @param count the number of bytes is available at this time
	 * @return the number of bytes handled by this call, if the return value is larger than 0 and the requested size
	 *         limit not reach, then the remaning data from the token stream will keep sent to the handler, until
	 *         it returns an error code or 0
	 **/
	size_t (*data_handler)(void* __restrict context, const void* __restrict data, size_t count);
} runtime_api_scope_token_data_request_t;

/**
 * @brief The callback when the type of the pipe is determined
 * @param pipe the pipe descriptor
 * @param type_name the concrete type name of the pipe
 * @param data the addtional data passed to the callback, typically the servlet instance context
 * @return status code
 **/
typedef int (*runtime_api_pipe_type_callback_t)(runtime_api_pipe_t pipe, const char* type_name, void* data);

/**
 * @brief The return value for the servlet's init function, which indicates the property of the servlet
 **/
enum {
	RUNTIME_API_INIT_RESULT_SYNC   = 0,    /*!< This is a sync servlet */
	RUNTIME_API_INIT_RESULT_ASYNC  = 1,    /*!< This is an async servlet */
};
STATIC_ASSERTION_EQ(RUNTIME_API_INIT_RESULT_SYNC, 0);

/**
 * @brief This is the dummy type we used to make the compiler aware we are dealing with a async task handle
 **/
typedef struct _runtime_api_async_task_handle_t runtime_api_async_handle_t;


/**
 * @brief the address table that contains the address of the pipe APIs
 * @note we do not need the servlet instance id, because the caller of the exec of the init will definately have the execution info. <br/>
 *       All the function defined in this place can only be called within the servlet context. <br/>
 **/
typedef struct {
	/**
	 * @brief define a named pipe in the PDT of this servlet
	 * @details Define a IO pipe for a servlet. The function will define a pipe for a servlet.
	 *         Which is actually either the input or the output end of a pipe. <br/>
	 *         The function will return a integer called pipe descriptor. The pipe descriptor can be
	 *         used later in the exec function of the servlet as the data source/sink. <br/>
	 *         For the given servlet instance, once the instance is initialized, the servlet can not
	 *         define the pipe anymore. <br/>
	 *         The reason for why we need such limitation is the topologic structure of a service *must*
	 *         be defined before the framework start serving traffic. Which means, we do not allow the
	 *         node change it's layout after the initialization pharse. <br/>
	 *         Also, the pipe can be defined with different properties. The properties can be changed at
	 *         the execution pharse for *only that exuection*. Which means you can not preserve the pipe
	 *         flags changed in execution phrase.
	 * @note This function must be called by the **init** function in servlet
	 * @param name the name to this pipe
	 * @param flag the flag to create this pipe
	 * @param type_expr the type expression for this pipe, if NULL is given, then this is a untyped pipe
	 * @return pipe id or a negative error code
	 **/
	runtime_api_pipe_t   (*define)        (const char* name, runtime_api_pipe_flags_t flag, const char* type_expr);

	/**
	 * @brief setup the hook function when the type of pipe is determined. The reason for having this function is
	 *        that we have some generic typed servlet. So the type of the pipe is depends on the context of the service
	 *        graph, rather than the servlet itself. So that we do not know the concrete type of the generic pipe until
	 *        we finished the type inference. <br/>
	 *        However, we don't want to query the type info in exeuction time, because of the perofmrance consideration.
	 *        So we should have some mechanism so that we can initialize the type specified information before the servlet
	 *        actually started.
	 * @param callback the callback function pointer
	 * @param pipe     the pipe descriptor we want to set the callback
	 * @param data     the additional data to be passed to the callback function
	 * @note  Because the type inferrer only work on the assigned pipes, so even though the type of unassigned pipes are known
	 *        the callback function won't be called.
	 * @return status code
	 * @todo implement this
	 **/
	int                  (*set_type_hook) (runtime_api_pipe_t pipe, runtime_api_pipe_type_callback_t callback, void* data);

	/**
	 * @brief read data from pipe
	 * @param pipe the pipe to read
	 * @param nbytes the number of bytes to read
	 * @param buffer the memory buffer for the read result
	 * @return either the number of bytes or the error code
	 **/
	size_t   (*read)         (runtime_api_pipe_t pipe, void* buffer, size_t nbytes);

	/**
	 * @brief write data to the given pipe
	 * @param pipe the pipe to write
	 * @param data the data buffer
	 * @param nbytes the number of bytes to write
	 * @return number of bytes has written
	 **/
	size_t   (*write)        (runtime_api_pipe_t pipe, const void* data, size_t nbytes);

	/**
	 * @brief write the content of a scope token to the pipe
	 * @details The detailed description can be found in the documentation of the module call write_callback
	 * @param pipe the pipe to write
	 * @param token the token
	 * @param data_req the data request callback, see the type documentation for the details about the data request mechanism.
	 *                 If the data request is not desired, pass just NULL. During this time, no pointer's ownership will be taken
	 * @note this function will make sure that all the bytes for this token is written to the pipe
	 * @return status code
	 **/
	int      (*write_scope_token) (runtime_api_pipe_t pipe, runtime_api_scope_token_t token, const runtime_api_scope_token_data_request_t* data_req);

	/**
	 * @brief write a log to the plumber logging system
	 * @param level the log level
	 * @param file  the source code file name
	 * @param function the function name
	 * @param line the line number
	 * @param fmt the formatting string
	 * @param ap the vaargs
	 * @return nothing
	 **/
	void  (*log_write)    (int level, const char* file, const char* function,
	                       int line, const char* fmt, va_list ap);
	/**
	 * @brief trap is a function that makes the execution flow goes back to the Plumber framework
	 * @param id the trap id
	 **/
	void  (*trap)     (int id);

	/**
	 * @brief EOF check
	 * @param pipe the pipe id
	 * @return result or status code
	 **/
	int   (*eof)     (runtime_api_pipe_t pipe);

	/**
	 * @brief the pipe control API
	 * @note this function is used to control pipe behaviour, like POSIX API fcntl. This function
	 *       modifies the current pipe instance only, and do not affect any other pipe instnaces.
	 * @param pipe the target pipe
	 * @param opcode what operation needs to be perfomed
	 * @param ap the va params
	 * @return the status code
	 **/
	int   (*cntl)    (runtime_api_pipe_t pipe, uint32_t opcode, va_list ap);

	/**
	 * @brief requires a module function handle pipe so that the servlet can call the service module
	 * @param mod_name the name of the service module function, e.g. "mempool.object_pool"
	 * @param func_name the name of the function we want, e.g. "allocate"
	 * @return the module type code
	 **/
	runtime_api_pipe_t (*get_module_func) (const char* mod_name, const char* func_name);

	/**
	 * @brief open a module by its name
	 * @param mod the name of the module
	 * @note this function must match the exact module path
	 * @return the module type code or error code
	 **/
	uint8_t (*mod_open) (const char* mod);

	/**
	 * @brief get the 8bit prefix used for the module specified cntl opcode
	 * @param path the path to the module suffix, for example all the TLS module should use "pipe.tls"
	 * @param result the result prefix, if there's no module instace under the given path, the result will be set to ERROR_CODE(uint8_t)
	 * @note The reason why we need this function is: <br/>
	 *       1. The module is dynmaically loaded, so the module id is determined in runtime <br/>
	 *       2. The servlet API is module-implementation-transparent, which means we cannot put any pipe implementation specified code in the
	 *          servlet code <br/>
	 *       In some cases, we may have serveral module instatiated from the same module binary
	 *       with different module initializtion param.
	 *       In this case, if we need to call the module specified control opcode, we *have to* know
	 *       the module id, because the opcode for module specified opcode is &lt;module-id, module-specfied-opcode&gt; <br/>
	 *       The module id can be get from mod_open call, however, it requires a full path to the module instance. This
	 *       makes the details of the pipe not transparent to the servlet. <br/>
	 *       For example, in code we may want to disable the TLS encrpytion because of the oppurtunistic encryption.
	 *       On the server which is configured the TLS module is listening to the port 443. With mod_open call, we have to
	 *       make the code like:
	 *       \code{.c}
	 *       	uint8_t mod = mod_open("pipe.tls.pipe.tcp.port_443");
	 *       	uint32_t opcode = (mod << 24) | (OPCODE_WE_WANT);
	 *       	pipe_cntl(pipe, opcode, ....);
	 *       \endcode
	 *       As we can see from the code, once the port gets changed, the servlet doesn't work anymore.
	 *       So it's not pipe transparent. <br/>
	 *       To address this issue, we actually use *one representitive of all the module instances that is initialized from the same module binary*.
	 *       Because all the module binary are the same, so the ITC framework will be able to call the correct module binary.
	 *       At the same time, because the pipe itself has a reference to the module instance context, so the call will be forwarded correctly. <br/>
	 *       On the other hand, it's reasonable for all the module instance from same binary (e.g. all the TLS modules) to have the same opcode
	 *       because it's reasonable to have all those module instances gets the same opcode. <br/>
	 *       Based on the reason above we need the function that can return a "representitive module instance" for all the module which creates from the
	 *       same module binary. <br/>
	 *       In this function, we need assume that all the module instances under the given path are created from the same module binary.
	 *       If this rule breaks, it will return an error code
	 * @return status code
	 **/
	int (*mod_cntl_prefix)(const char* path, uint8_t* result);

	/**
	 * @brief get the plumber version number
	 * @return the plumber version number string, NULL on error
	 **/
	const char* (*version)(void);

	/**
	 * @brief The async task control function
	 * @note This function is the only plumber API can be called from the async processing thread.
	 *       And we actually have the limit for this function is we should call this function with task context,
	 *       otherwise we should provide the task_handle (The example for this case is the ASYNC_CNTL_OPCODE_NOTIFY_WAIT,
	 *       which we may not call the function from the thread working on the async task. In this case, we just extract
	 *       the async context from the handle
	 * @todo implement this
	 * @return status code
	 **/
	int (*async_cntl)(runtime_api_async_handle_t* async_handle, uint32_t opcode, va_list ap);
} runtime_api_address_table_t;

/**
 * @brief the data structure used to define a servlet
 * @note  Instead of checking if the callback fuction is defined to determine if this is an async task. We relies on the
 *        return value of the init function. This is because for the language support servlet, we can not tell if it's a
 *        async servlet or not during the compile time. The only way for us to kown this is get the servlet fully initialized<br/>
 *        If the init function returns RUNTIME_API_INIT_RESULT_SYNC and exec function has been defined, all the async_* function
 *        won't be used any mopre. This case indicates we have a sync servlet. <br/>
 *        If the init function returns RUNTIME_API_INIT_RESULT_ASYNC, asyc_init must be defined
 *        This case indicates we have an async servlet <br/>
 *        The async_exec and async_cleanup function is not necessarily to be defined. Because for some case, we
 *        actually can have a task initialize a async IO form the async_init and set the async task mode to the wait mode
 *        Then we can have an undefined async_exec function, which means we don't need to do anything other than initializing
 *        the IO. <br/>
 *        If the exec function is not defined and init returns RUNTIME_API_INIT_RESULT_SYNC, this indicates we have an sync servlet
 *        with an empty exec function. This is useful when we only have a shadow output for the servlet, for example, dataflow/dup.
 * @todo  When a servlet is loaded we should
 **/
typedef struct {
	size_t size;					   /*!< the size of the additional data for this servlet */
	const char* desc;				   /*!< the description of this servlet */
	uint32_t version;				   /*!< the required API version for this servlet, currently is 0 */
	/**
	 * @brief The function that will be called by the initialize task
	 * @param argc the argument count
	 * @param argv the value list of arguments
	 * @param data the servlet local data that needs to be intialized
	 * @return The servlet property flags, or error code
	 **/
	int (*init)(uint32_t argc, char const* const* argv, void* data);
	/**
	 * @brief the function that will be called by the execution task
	 * @param data the servlet local data
	 * @return status code
	 **/
	int (*exec)(void* data);
	/**
	 * @brief the function that will be called by the finalization task
	 * @param data the local data that need to be handled by the servlet
	 * @return status code
	 **/
	int (*unload)(void* data);

	/** The async task part, and it should be NULL and 0 if servlet has a exec callback */

	uint32_t async_buf_size;   /*!< The async buffer size */

	/**
	 * @brief The initialization stage of the async task
	 * @param task This is the handle we used to pass to the async_cntl funciton
	 * @param data The servlet local context
	 * @param async_buf The buffer we are going to carry to the async_exec
	 * @note For the async_exec function, we don't allow the servlet access any servlet context,
	 *       because this breaks the thread convention of the worker thread.
	 *       This make the async task has to copy all the required data to the async buf, which is
	 *       complete different memory, and this memory will be the only data the async_exec function
	 *       can access
	 * @return status code
	 **/
	int (*async_setup)(runtime_api_async_handle_t* task, void* async_buf, void* data);

	/**
	 * @brief Execute the initialized async task, the only input of the async buf is the async buf
	 *        In this function, all the API calls are disallowed.
	 * @param async_buf The async data buffer
	 * @param task This is the handle we used to pass to the async_cntl funciton
	 * @return status code
	 **/
	int (*async_exec)(runtime_api_async_handle_t* task, void* async_data);

	/**
	 * @brief Clean the used async data
	 * @param data The servlet local context data
	 * @param task_handle This is the handle we used to pass to the async_cntl funciton
	 * @return status code
	 **/
	int (*async_cleanup)(runtime_api_async_handle_t* task, void* async_data, void* data);
} runtime_api_servlet_def_t;

#endif /*__RUNTIME_API_H__*/
