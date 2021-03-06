/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <error.h>
#include <utils/static_assertion.h>

#include <pservlet.h>
#include <package_config.h>
#include <proto.h>

#include <pstd/type.h>

/**
 * @brief The internal alias for the pstd_typeinfo_accessor_t
 **/
typedef pstd_type_accessor_t _acc_t;

/**
 * @brief Represent a accessor data
 * @note  Because we allows type variables, so that we actually do not
 *        query the type info util the type callback is called
 **/
typedef struct _accessor_t {
	uint32_t            init:1;         /*!< If this accessor is initialized */
	char*               field;          /*!< The field expression we want to query */
	pipe_t              pipe;           /*!< The target pipe */
	uint32_t            offset;         /*!< The begining of the interested memory region */
	uint32_t            size;           /*!< The size of the interested memory region */
	uint32_t            next;           /*!< The next accessor for the same pipe */
} _accessor_t;

/**
 * @brief The type assertion object
 **/
typedef struct _type_assertion_t {
	pstd_type_assertion_t       func; /*!< The assertion function */
	void*                       data; /*!< The additional data for the assertion function */
	struct _type_assertion_t*   next; /*!< The next pointer */
} _type_assertion_t;

/**
 * @brief Represent a request for the field information
 **/
typedef struct _field_req_t {
	char*                 field;    /*!< The field expression for this field information request */
	pstd_type_field_t*   info_buf; /*!< The buffer to return the information */
	struct _field_req_t* next;  /*!< The next pointer in the linked list */
} _field_req_t;


/**
 * @brief The type const object
 **/
typedef struct _const_t {
	char*    field;      /*!< The field name for the const */
	uint32_t is_real:1;  /*!< If this is the real number */
	uint32_t is_signed:1;/*!< If this is a signed value */
	uint32_t size;       /*!< The size of this value */
	void*    target;     /*!< The buffer for this value */
	struct _const_t* next;  /*!< The next const definition */
} _const_t;

/**
 * @brief Represent the type information for one pipe
 **/
typedef struct {
	uint32_t                cb_setup:1;    /*!< Indicates if we have already installed the type callback for this type info */
	uint32_t                init:1;        /*!< If the type info has been initialized */
	pipe_t                  copy_from;     /*!< If given it indicates which pipe we need to copy data from at the begning */
	char*                   name;          /*!< The name of the type */
	uint32_t                full_size;     /*!< The size of the header section */
	uint32_t                used_size;     /*!< The size of the header data we actually used */
	size_t                  buf_begin;     /*!< The offest for buffer of the type context isntance for this pipe begins */
	uint32_t                accessor_list; /*!< The list of accessors related to this pipe */
	_const_t*               const_list;    /*!< The list of the constant defined by this pipe */
	_type_assertion_t*      assertion_list;/*!< The assertion list */
	_field_req_t*           field_list;     /*!< The field request list */
} _typeinfo_t;

/**
 * @brief The type context for a servlet instance
 **/
struct _pstd_type_model_t {
	uint32_t                     pipe_cap;    /*!< The pipe info array capacity */
	runtime_api_pipe_id_t        pipe_max;    /*!< The upper bound of the pipe id */
	_typeinfo_t*                 type_info;   /*!< The type information */
	_acc_t                       accessor_cap;/*!< The capacity of the accessor table */
	_acc_t                       accessor_cnt;/*!< The size of the accessor table */
	_accessor_t*                 accessor;    /*!< The accessor table */
};

/**
 * @brief The pipe header
 **/
typedef struct __attribute__((packed)) {
	size_t valid_size;   /*!< The valid size */
	char   data[0];      /*!< The data buffer */
	char*  bufptr[0];    /*!< The buffer pointer */
} _header_buf_t;

/**
 * @brief The type context instance
 **/
struct _pstd_type_instance_t {
	uint32_t                    heapmem:1;   /*!< Indicates if this instance uses the heap memory */
	const pstd_type_model_t*    model;       /*!< The underlying type model */
	uintpad_t __padding__[0];
	char                        buffer[0];   /*!< The actual buffer memory */
};

/**
 * @brief Output the libproto error information in the log
 * @param err The error object
 * @return nothing
 **/
static inline void _proto_err_stack(const proto_err_t* err)
{
#ifdef LOG_ERROR_ENABLED
	char buffer[128];

	if(NULL == err && NULL == (err = proto_err_stack())) return;

	LOG_ERROR("libproto error: %s", proto_err_str(err, buffer, sizeof(buffer)));
	if(NULL != err->child)
	    _proto_err_stack(err->child);
#else /* LOG_ERROR_ENABLED */
	(void)err;
#endif /* LOG_ERROR_ENABLED */
}

static inline int _get_effective_field(const char* master_type, const char* encapsulated,
                                      const char* field_expr,
                                      char* buffer, size_t buf_size,
                                      char const* * effective_type, char const* * effective_field_expr)
{
	int encapsulate_level = 0;
	for(;field_expr[encapsulate_level] == '*'; encapsulate_level ++);
	if(encapsulate_level == 0)
	{
		*effective_type = master_type;
		*effective_field_expr = field_expr;
		return 0;
	}

	*effective_field_expr = field_expr + encapsulate_level;

	const char* begin = NULL;

	for(;encapsulate_level > 0 && encapsulated && encapsulated[0]; encapsulate_level --)
	{
		begin = encapsulated + 1;
		encapsulated = strchr(encapsulated + 1, ' ');
	}

	if(encapsulate_level > 0)
	    ERROR_RETURN_LOG(int, "Not a encapsulated type");

	if(NULL == encapsulated)
	    encapsulated = begin + strlen(begin);

	if(buf_size < (size_t)(encapsulated - begin + 1))
	    ERROR_RETURN_LOG(int, "Type name is too long");

	memcpy(buffer, begin, (size_t)(encapsulated - begin));

	buffer[encapsulated - begin] = 0;

	*effective_type = buffer;

	return 0;
}

/**
 * @brief The callback function that fill up the type related data when the type is determined by
 *        the framework
 * @param pipe which pipe we are talking about
 * @param typename the name of the actual type of the pipe
 * @param data the related typeinfo object
 * @return status code
 **/
static int _on_pipe_type_determined(pipe_t pipe, const char* typename, void* data)
{
	pstd_type_model_t* model = (pstd_type_model_t*)data;
	_typeinfo_t* typeinfo = model->type_info + PIPE_GET_ID(pipe);

	/* Duplicate the typename */
	size_t namelen = 0;

	for(;typename[namelen] && typename[namelen] != ' '; namelen ++);

	if(NULL == (typeinfo->name = (char*)malloc(namelen + 1)))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the type name");

	memcpy(typeinfo->name, typename, namelen);
	typeinfo->name[namelen] = 0;

	int rc  = ERROR_CODE(int);

	if(ERROR_CODE(int) == proto_init())
	    ERROR_LOG_GOTO(ERR, "Cannot initialize libproto");

	/* Get the full size of the header */
	if(ERROR_CODE(uint32_t) == (typeinfo->full_size = proto_db_type_size(typeinfo->name)))
	    ERROR_LOG_GOTO(ERR, "Cannot get the full size of type %s", typeinfo->name);


	/* Check all the assertions */
	_type_assertion_t* assertion;
	for(assertion = typeinfo->assertion_list; NULL != assertion; assertion = assertion->next)
	    if(ERROR_CODE(int) == assertion->func(pipe, typename, assertion->data))
	        ERROR_LOG_GOTO(ERR, "Type assertion failed");

	/* Fetch all the field request */
	_field_req_t* field_req;
	for(field_req = typeinfo->field_list; NULL != field_req; field_req = field_req->next)
	{
		const char* effective_type = NULL;
		const char* effective_field = NULL;
		char buffer[PATH_MAX];
		if(ERROR_CODE(int) == _get_effective_field(typeinfo->name, typename + namelen, field_req->field,
		                                           buffer, sizeof(buffer), &effective_type, &effective_field))
		    ERROR_LOG_GOTO(ERR, "Cannot parse the effective field name");

		proto_db_field_prop_t prop;
		if(ERROR_CODE(int) == (prop = proto_db_field_type_info(effective_type, effective_field)))
		    ERROR_LOG_GOTO(ERR, "Cannot query the field type property");

		field_req->info_buf->is_numeric = ((prop & PROTO_DB_FIELD_PROP_NUMERIC) > 0);
		field_req->info_buf->is_signed = ((prop & PROTO_DB_FIELD_PROP_SIGNED) > 0);
		field_req->info_buf->is_float = ((prop & PROTO_DB_FIELD_PROP_REAL) > 0);
		field_req->info_buf->is_token = ((prop & PROTO_DB_FIELD_PROP_SCOPE) > 0);
		field_req->info_buf->is_primitive_token = ((prop & PROTO_DB_FIELD_PROP_PRIMITIVE_SCOPE) > 0);
		field_req->info_buf->is_compound = (prop == 0);

		if(ERROR_CODE(uint32_t) == (field_req->info_buf->offset = proto_db_type_offset(effective_type, effective_field, &field_req->info_buf->size)))
		    ERROR_LOG_GOTO(ERR, "Cannot query the offset of the field");
	}

	/* Then we need to fetch all the constants */
	_const_t* constant;
	for(constant = typeinfo->const_list; NULL != constant; constant = constant->next)
	{
		const char* effective_type = NULL;
		const char* effective_field = NULL;
		char buffer[PATH_MAX];
		if(ERROR_CODE(int) == _get_effective_field(typeinfo->name, typename + namelen, constant->field,
		                                           buffer, sizeof(buffer), &effective_type, &effective_field))
		    ERROR_LOG_GOTO(ERR, "Cannot parse the effective field name");

		proto_db_field_prop_t prop = proto_db_field_type_info(effective_type, effective_field);
		if(ERROR_CODE(int) == prop)
		    ERROR_LOG_GOTO(ERR, "Cannot query the field type property for field %s of type %s", effective_field, effective_type);

		if(!(prop & PROTO_DB_FIELD_PROP_NUMERIC))
		    ERROR_LOG_GOTO(ERR, "Type error: numeric type expected for a constant");

		const void* data_ptr;
		size_t size;

		if(1 != proto_db_field_get_default(effective_type, effective_field, &data_ptr, &size))
		    ERROR_LOG_GOTO(ERR, "Cannot get the default value of the field");

		if(!(prop & PROTO_DB_FIELD_PROP_REAL))
		{
			/* This is an interger value */
			if(constant->is_real) ERROR_LOG_GOTO(ERR, "Type error: integer value expected, but floating point number got");
			if(((prop & PROTO_DB_FIELD_PROP_SIGNED) != 0) ^ constant->is_signed)
			    ERROR_LOG_GOTO(ERR, "Type error: signedness mismatch");
			if(size  > constant->size)
			    ERROR_LOG_GOTO(ERR, "Type error: the integer constant has been truncated");
			memcpy(constant->target, data_ptr, size);
			uint8_t* u8 = (uint8_t*)constant->target;
			/* If this is a signed value, we should expand the sign bit */
			if(constant->is_signed && (u8[size - 1]&0x80)) u8[size - 1] &= 0x7f, u8[constant->size - 1] |= 0x80;
		}
		else
		{
			/* This is a real number */
			if(!constant->is_real) ERROR_LOG_GOTO(ERR, "Type error: floating point value expected, but integer number got");
			if(size == 4 && constant->size == 4) *(float*)constant->target = *(const float*)data_ptr;
			if(size == 4 && constant->size == 8) *(double*)constant->target = *(const float*)data_ptr;
			if(size == 8 && constant->size == 4) *(float*)constant->target = (float)*(const double*)data_ptr;
			if(size == 8 && constant->size == 8) *(double*)constant->target = *(const double*)data_ptr;
		}
	}

	/* Fill the offset info into accessors */
	uint32_t offset;
	_accessor_t* accessor;
	for(offset = typeinfo->accessor_list; ERROR_CODE(uint32_t) != offset; offset = accessor->next)
	{
		accessor = model->accessor + offset;
		if(ERROR_CODE(uint32_t) == (accessor->offset = proto_db_type_offset(typeinfo->name, accessor->field, &accessor->size)))
		    ERROR_LOG_GOTO(ERR, "Cannot get the type param for %s.%s", typeinfo->name, accessor->field);
		accessor->init = 1;

		if(typeinfo->used_size < accessor->offset + accessor->size)
		    typeinfo->used_size = accessor->offset + accessor->size;
	}

	/* Update the buffer offest information.
	 * Yes, at this point the approach is not optimal, we spend O(N^2) time to update the buffer offset.
	 * But it's Ok, because we are actually in initialization pharse */
	if(typeinfo->used_size > 0)
	{
		runtime_api_pipe_id_t i;
		for(i = (runtime_api_pipe_id_t)(PIPE_GET_ID(pipe) + 1); i < model->pipe_max; i ++)
		    model->type_info[i].buf_begin += typeinfo->used_size + sizeof(_header_buf_t);
	}

	typeinfo->init = 1u;

	/* Finally we update the size info if we have some copy request */
	{
		runtime_api_pipe_id_t p_dst;
		for(p_dst = 0; p_dst < model->pipe_max; p_dst ++)
		    if(model->type_info[p_dst].copy_from != ERROR_CODE(pipe_t))
		    {
			    runtime_api_pipe_id_t p_src = (runtime_api_pipe_id_t)PIPE_GET_ID(model->type_info[p_dst].copy_from);
			    if(p_src >= model->pipe_max)
			        ERROR_LOG_GOTO(ERR, "Invalid source pipe %u", p_src);
			    if(model->type_info[p_src].init && model->type_info[p_dst].init)
			    {
				    const char* from_type = model->type_info[p_src].name;
				    const char* to_type = model->type_info[p_dst].name;
				    const char* types[] = {from_type, to_type, NULL};

				    const char* common = proto_db_common_ancestor(types);
				    if(NULL == common || strcmp(common, to_type) != 0)
				        ERROR_LOG_GOTO(ERR, "Invalid pipe data copy: from %s to %s", from_type, to_type);

				    uint32_t required_size = model->type_info[p_dst].full_size;

				    pipe_t p[2] = {(p_src < p_dst ? p_src : p_dst), (p_src < p_dst ? p_dst : p_src)};
				    runtime_api_pipe_id_t i;
				    uint32_t j;
				    size_t delta = 0;

				    /* Then we need to check all the buffer allocation fulfills the requirement */
				    for(i = 0, j = 0; i < model->pipe_max; i ++)
				    {
					    model->type_info[i].buf_begin += delta;
					    if(j < 2 && p[j] == i)
					    {
						    if(model->type_info[p[j]].used_size < required_size)
						    {
							    if(model->type_info[p[j]].used_size == 0)
							        delta += sizeof(_header_buf_t);
							    delta += (uint32_t)(required_size - model->type_info[p[j]].used_size);
							    model->type_info[p[j]].used_size = required_size;
						    }
						    j++;
					    }
				    }
			    }
		    }
	}

	rc = 0;

ERR:

	/* Because even though it's error case, but the memory allocated for the type name is assigned to
	 * typeinfo->name, and will be disposed when the type context is getting disposed. So we do not
	 * need to do anything about the name buffer at this point */

	if(ERROR_CODE(int) == proto_finalize())
	{
		rc = ERROR_CODE(int);
		LOG_ERROR("Cannot finalize libproto");
	}

	if(ERROR_CODE(int) == rc)
	{
		LOG_ERROR("===========libproto error stack============");
		_proto_err_stack(NULL);
		LOG_ERROR("===========================================");
	}
	return rc;
}

/**
 * @brief Ensure the type_info array have a slot for the given pipe
 * @param ctx The type context
 * @param pipe The pipe descriptor
 * @return status code
 **/
static inline int _ensure_pipe_typeinfo(pstd_type_model_t* ctx, pipe_t pipe)
{
	runtime_api_pipe_id_t pid = PIPE_GET_ID(pipe);

	if(ctx->pipe_cap <= pid + 1u)
	{
		_typeinfo_t* newbuf = (_typeinfo_t*)realloc(ctx->type_info, sizeof(ctx->type_info[0]) * ctx->pipe_cap * 2);
		if(NULL == newbuf)
		    ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the type info array");

		memset(newbuf + sizeof(ctx->type_info[0]) * ctx->pipe_cap, 0,  sizeof(ctx->type_info[0]) * ctx->pipe_cap);

		uint32_t i;
		for(i = ctx->pipe_cap; i < ctx->pipe_cap * 2; i ++)
		{
			ctx->type_info[i].accessor_list = ERROR_CODE(uint32_t);
			ctx->type_info[pid].copy_from = ERROR_CODE(pipe_t);
		}

		ctx->pipe_cap <<= 1u;
		ctx->type_info = newbuf;
	}

	/* We don't need to update the buf_begin field, because we current don't know the size, and all
	 * the offset are 0 */
	if(ctx->pipe_max < pid + 1u)
	    ctx->pipe_max = (runtime_api_pipe_id_t)(pid + 1u);

	if(!ctx->type_info[pid].cb_setup)
	{
		if(ERROR_CODE(int) == pipe_set_type_callback(pipe, _on_pipe_type_determined, ctx))
		    ERROR_RETURN_LOG(int, "Cannot setup the type callback function for the pipe");

		ctx->type_info[pid].cb_setup = 1;
	}



	return 0;
}

/**
 * @brief Allocate a new type accessor in the given context
 * @param ctx The context
 * @param pipe The pipe owns this accessor
 * @param field_expr The field expression
 * @return The accessor id or error code
 **/
static inline _acc_t _accessor_alloc(pstd_type_model_t* ctx, pipe_t pipe, const char* field_expr)
{
	if(ctx->accessor_cap <= ctx->accessor_cnt)
	{
		_accessor_t* newbuf = (_accessor_t*)realloc(ctx->accessor, sizeof(ctx->accessor[0]) * ctx->accessor_cap * 2);
		if(NULL == newbuf)
		    ERROR_RETURN_LOG_ERRNO(_acc_t, "Cannot resize the accessor array");

		ctx->accessor_cap *= 2;
		ctx->accessor = newbuf;
	}

	_accessor_t* accessor = ctx->accessor + ctx->accessor_cnt;

	accessor->init = 0;

	size_t len = strlen(field_expr) + 1;

	if(NULL == (accessor->field = (char*)malloc(len)))
	    ERROR_RETURN_LOG_ERRNO(_acc_t, "Cannot allocate the field expression buffer");

	memcpy(accessor->field, field_expr, len);

	accessor->pipe = pipe;

	if(ERROR_CODE(int) == _ensure_pipe_typeinfo(ctx, pipe))
	    ERROR_RETURN_LOG_ERRNO(_acc_t, "Cannot resize the typeinfo array");

	accessor->next = ctx->type_info[PIPE_GET_ID(pipe)].accessor_list;
	ctx->type_info[PIPE_GET_ID(pipe)].accessor_list = (uint32_t)(accessor - ctx->accessor);

	return ctx->accessor_cnt ++;
}

pstd_type_model_t* pstd_type_model_new()
{
	pstd_type_model_t* ret = (pstd_type_model_t*)calloc(1, sizeof(pstd_type_model_t));

	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the type model");

	ret->pipe_max = 0;
	ret->pipe_cap = PSTD_TYPE_MODEL_PIPE_VEC_INIT_CAP;

	if(NULL == (ret->type_info = (_typeinfo_t*)calloc(ret->pipe_cap, sizeof(ret->type_info[0]))))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for for the typeinfo array");

	uint32_t i;
	for(i = 0; i < ret->pipe_cap; i ++)
	{
		ret->type_info[i].accessor_list = ERROR_CODE(uint32_t);
		ret->type_info[i].copy_from = ERROR_CODE(pipe_t);
	}

	ret->accessor_cnt = 0;
	ret->accessor_cap = PSTD_TYPE_MODEL_ACC_VEC_INIT_CAP;
	if(NULL == (ret->accessor = (_accessor_t*)malloc(ret->accessor_cap * sizeof(ret->accessor[0]))))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the accessor array");

	return ret;
ERR:
	if(NULL != ret)
	{
		if(NULL != ret->type_info) free(ret->type_info);
		if(NULL != ret->accessor)  free(ret->accessor);
	}

	return NULL;
}

int pstd_type_model_free(pstd_type_model_t* model)
{
	if(NULL == model)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(model->accessor != NULL)
	{
		_acc_t i;
		for(i = 0; i < model->accessor_cnt; i ++)
		{
			if(NULL != model->accessor[i].field)
			    free(model->accessor[i].field);
		}

		free(model->accessor);
	}

	if(model->type_info != NULL)
	{
		runtime_api_pipe_id_t i;
		for(i = 0; i < model->pipe_max; i ++)
		{
			_type_assertion_t* ptr;
			for(ptr = model->type_info[i].assertion_list; NULL != ptr;)
			{
				_type_assertion_t* cur = ptr;
				ptr = ptr->next;

				free(cur);
			}

			_const_t* con;
			for(con = model->type_info[i].const_list; NULL != con;)
			{
				_const_t* cur = con;
				con = con->next;
				if(cur->field != NULL) free(cur->field);
				free(cur);
			}

			_field_req_t* req;
			for(req = model->type_info[i].field_list; NULL != req;)
			{
				_field_req_t* cur = req;
				req = req->next;

				if(cur->field != NULL) free(cur->field);
				free(cur);
			}

			if(NULL != model->type_info[i].name)
			    free(model->type_info[i].name);
		}

		/* We do not need to dispose the accessor list, because we do not hold the ownership of the accessors */

		free(model->type_info);
	}

	free(model);

	return 0;
}

pstd_type_accessor_t pstd_type_model_get_accessor(pstd_type_model_t* model, pipe_t pipe, const char* field_expr)
{
	if(NULL == model || NULL == field_expr || RUNTIME_API_PIPE_IS_VIRTUAL(pipe) || ERROR_CODE(pipe_t) == pipe)
	    ERROR_RETURN_LOG(pstd_type_accessor_t, "Invalid arguments");

	if(*field_expr == '*')
	    ERROR_RETURN_LOG(pstd_type_accessor_t, "Encapsulated type doesn't support accessor");

	return _accessor_alloc(model, pipe, field_expr);
}

int pstd_type_model_assert(pstd_type_model_t* model, pipe_t pipe, pstd_type_assertion_t assertion, void* data)
{
	if(NULL == model || NULL == assertion || ERROR_CODE(pipe_t) == pipe || RUNTIME_API_PIPE_IS_VIRTUAL(pipe))
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == _ensure_pipe_typeinfo(model, pipe))
	    ERROR_RETURN_LOG(int, "Cannot resize the typeinfo array");

	_typeinfo_t* typeinfo = model->type_info + PIPE_GET_ID(pipe);

	_type_assertion_t* obj = (_type_assertion_t*)malloc(sizeof(*obj));
	if(NULL == obj)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the type assertion object");

	obj->func = assertion;
	obj->data = data;
	obj->next = typeinfo->assertion_list;
	typeinfo->assertion_list = obj;

	return 0;
}

int pstd_type_model_get_field_info(pstd_type_model_t* model, pipe_t pipe, const char* field_expr, pstd_type_field_t* buf)
{
	if(NULL == model || NULL == field_expr || NULL == buf || ERROR_CODE(pipe_t) == pipe || RUNTIME_API_PIPE_IS_VIRTUAL(pipe))
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == _ensure_pipe_typeinfo(model, pipe))
	    ERROR_RETURN_LOG(int, "Cannot resize the typeinfo array");

	_typeinfo_t* typeinfo = model->type_info + PIPE_GET_ID(pipe);

	_field_req_t* req = (_field_req_t*)malloc(sizeof(*req));
	if(NULL == req)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the field request object");

	req->field = NULL;

	if(NULL == (req->field = strdup(field_expr)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the field string");

	req->info_buf = buf;

	req->next = typeinfo->field_list;
	typeinfo->field_list = req;

	return 0;
ERR:
	if(NULL != req->field) free(req->field);
	free(req);
	return ERROR_CODE(int);
}

int pstd_type_model_const(pstd_type_model_t* model, pipe_t pipe, const char* field, int is_signed, int is_real, void* buf, uint32_t bufsize)
{
	if(NULL == model || NULL == field || ERROR_CODE(pipe_t) == pipe || RUNTIME_API_PIPE_IS_VIRTUAL(pipe))
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == _ensure_pipe_typeinfo(model, pipe))
	    ERROR_RETURN_LOG(int, "Cannot resize the typeinfo arrray");

	_typeinfo_t* typeinfo = model->type_info + PIPE_GET_ID(pipe);

	_const_t* obj = (_const_t*)malloc(sizeof(*obj));
	if(NULL == obj)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the constant object");

	obj->field = NULL;
	obj->is_signed = (is_signed != 0);
	obj->is_real   = (is_real != 0);
	obj->target    = buf;
	obj->size      = bufsize;
	if(NULL == (obj->field = strdup(field)))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot duplicate the field string");
	obj->next = typeinfo->const_list;
	typeinfo->const_list = obj;

	return 0;

ERR:
	if(obj->field != NULL) free(obj->field);
	free(obj);
	return ERROR_CODE(int);
}

int pstd_type_model_copy_pipe_data(pstd_type_model_t* model, pipe_t from, pipe_t to)
{
	if(NULL == model || ERROR_CODE(pipe_t) == from || ERROR_CODE(pipe_t) == to)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == _ensure_pipe_typeinfo(model, from))
	    ERROR_RETURN_LOG(int, "Cannot resize the typeinfo arrray");

	if(ERROR_CODE(int) == _ensure_pipe_typeinfo(model, to))
	    ERROR_RETURN_LOG(int, "Cannot resize the typeinfo arrray");

	_typeinfo_t* typeinfo = model->type_info + PIPE_GET_ID(to);

	typeinfo->copy_from = from;

	return 0;
}

/**
 * @brief Compute the instance buffer size
 * @param model the type model
 * @return size
 **/
static inline size_t _inst_buf_size(const pstd_type_model_t* model)
{
	if(model->pipe_max == 0) return 0;

	runtime_api_pipe_id_t last = (runtime_api_pipe_id_t)(model->pipe_max - 1);

	return (size_t)(model->type_info[last].buf_begin + model->type_info[last].used_size +
	                                                   (model->type_info[last].used_size > 0 ? sizeof(_header_buf_t) : 0));
}

size_t pstd_type_instance_size(const pstd_type_model_t* model)
{
	if(NULL == model)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	return sizeof(pstd_type_instance_t) + _inst_buf_size(model);
}

pstd_type_instance_t* pstd_type_instance_new(const pstd_type_model_t* model, void* mem)
{
	if(NULL == model)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	size_t size = sizeof(pstd_type_instance_t) + _inst_buf_size(model);
	pstd_type_instance_t* ret = (pstd_type_instance_t*)mem;

	if(NULL == ret && NULL == (ret = (pstd_type_instance_t*)malloc(size)))
	    ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the type context instance");

	ret->heapmem = (mem == NULL);
	ret->model = model;

	runtime_api_pipe_id_t i;
	for(i = 0; i < model->pipe_max; i ++)
	    if(model->type_info[i].used_size > 0)
	        *(size_t*)(ret->buffer + model->type_info[i].buf_begin) = 0;

	return ret;
}

static int _copy_header_data(pstd_type_instance_t* inst, pipe_t pipe) __attribute__((noinline));

int pstd_type_instance_free(pstd_type_instance_t* inst)
{
	if(NULL == inst)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	runtime_api_pipe_id_t i;
	int rc = 0;
	for(i = 0; i < inst->model->pipe_max; i ++)
	{

		if(!inst->model->type_info[i].init) continue;

		if(inst->model->type_info[i].used_size == 0)
		{
			if(inst->model->type_info[i].copy_from == ERROR_CODE(pipe_t))
			    continue;

			int copy_rc = _copy_header_data(inst, RUNTIME_API_PIPE_FROM_ID(i));
			if(copy_rc == ERROR_CODE(int))
			    ERROR_RETURN_LOG(int, "Cannot copy the data from the source pipe");

			if(!copy_rc) continue;
		}

		runtime_api_pipe_flags_t flags = PIPE_INPUT;

		if(ERROR_CODE(int) == pipe_cntl(RUNTIME_API_PIPE_FROM_ID(i), PIPE_CNTL_GET_FLAGS, &flags))
		{
			LOG_ERROR("Cannot get the pipe flag");
			rc = ERROR_CODE(int);
		}

		if(PIPE_FLAGS_IS_WRITABLE(flags))
		{
			const _header_buf_t* buf = (const _header_buf_t*)(inst->buffer + inst->model->type_info[i].buf_begin);
			const char* data = buf->data;
			size_t bytes_to_write = buf->valid_size;
			while(bytes_to_write > 0)
			{
				size_t bytes_written = pipe_hdr_write(RUNTIME_API_PIPE_FROM_ID(i), data, bytes_to_write);
				if(ERROR_CODE(size_t) == bytes_written)
				{
					LOG_ERROR("Cannot write header to the pipe, bytes remaining: %zu", bytes_to_write);
					rc = ERROR_CODE(int);
					break;
				}
				bytes_to_write -= bytes_written;
				data += bytes_written;
			}
		}
	}

	if(inst->heapmem)
	    free(inst);
	return rc;
}
/**
 * @brief Ensure we have read the nbytes-th bytes in the header
 * @param inst the type context instance
 * @param pipe the pipe we want to read
 * @param nbytes how many bytes we need to ensure
 * @return status code
 **/
static inline int _ensure_header_read(pstd_type_instance_t* inst, pipe_t pipe, size_t nbytes)
{
	const _typeinfo_t* typeinfo = inst->model->type_info + PIPE_GET_ID(pipe);
	_header_buf_t* buffer = (_header_buf_t*)(inst->buffer + typeinfo->buf_begin);

	/* If there are a buffer assocated with this type instance, just do nothing */
	if(buffer->valid_size == ERROR_CODE(uint32_t)) return 0;

	size_t bytes_can_read = typeinfo->used_size - buffer->valid_size;

	/* First try to use direct buffer access */
	if(buffer->valid_size == 0 && bytes_can_read >= sizeof(void*))
	{
		int rc = pipe_hdr_get_buf(pipe, bytes_can_read, (void const**)buffer->data);

		if(rc == ERROR_CODE(int))
		    ERROR_RETURN_LOG(int, "Cannot acquire the header internal buffer");

		if(rc == 0)
		    LOG_DEBUG("The direct buffer access is not possible, try to read directly");
		else
		{
			LOG_DEBUG("The direct buffer access has returned a buffer, use the buffer directly");
			buffer->valid_size = ERROR_CODE(size_t);
			return 0;
		}
	}

	while(buffer->valid_size < nbytes)
	{
		size_t rc = pipe_hdr_read(pipe, buffer->data + buffer->valid_size, bytes_can_read);
		if(ERROR_CODE(size_t) == rc)
		    ERROR_RETURN_LOG(int, "Cannot read header");

		if(rc == 0)
		{
			int eof_rc = pipe_eof(pipe);
			if(ERROR_CODE(int) == eof_rc)
			    ERROR_RETURN_LOG(int, "pipe_eof returns an error");

			if(eof_rc)
			{
				if(buffer->valid_size > 0)
				    ERROR_RETURN_LOG(int, "Unexpected end of data stream");
				else /* We are ok with the zero input */
				    return 0;
			}
		}

		bytes_can_read -= rc;
		buffer->valid_size += rc;
	}

	return 0;
}

size_t pstd_type_instance_field_size(pstd_type_instance_t* inst, pstd_type_accessor_t accessor)
{
	if(NULL == inst || ERROR_CODE(pstd_type_accessor_t) == accessor || accessor >= inst->model->accessor_cnt)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	const _accessor_t* obj = inst->model->accessor + accessor;

	return obj->init ? obj->size : 0;
}

size_t pstd_type_instance_read(pstd_type_instance_t* inst, pstd_type_accessor_t accessor, void* buf, size_t bufsize)
{
	if(NULL == inst || ERROR_CODE(pstd_type_accessor_t) == accessor || NULL == buf || accessor >= inst->model->accessor_cnt)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	const _accessor_t* obj = inst->model->accessor + accessor;

	/* It's possible the pipe is unassigned in the graph, so that we do not have any type info at this point
	 * In this case, we must stop at this point */
	if(!inst->model->accessor[accessor].init)
	    return 0;

	if(bufsize > obj->size) bufsize = obj->size;

	if(bufsize == 0) return 0;

	if(ERROR_CODE(int) == _ensure_header_read(inst, obj->pipe, obj->offset + bufsize))
	    ERROR_RETURN_LOG(size_t, "Cannot ensure the header buffer is valid");

	const _header_buf_t* buffer = (const _header_buf_t*)(inst->buffer + inst->model->type_info[PIPE_GET_ID(obj->pipe)].buf_begin);


	if(buffer->valid_size > 0 && buffer->valid_size != ERROR_CODE(size_t))
	    memcpy(buf, buffer->data + obj->offset, bufsize);
	else if(buffer->valid_size == ERROR_CODE(size_t))
	    memcpy(buf, buffer->bufptr[0] + obj->offset, bufsize);
	else
	    return 0;

	return bufsize;
}

__attribute__((noinline))
static int _copy_header_data(pstd_type_instance_t* inst, pipe_t pipe)
{
	uint32_t i = PIPE_GET_ID(pipe);
	const pstd_type_model_t* model = inst->model;

	pipe_t source = model->type_info[i].copy_from;
	int eof_rc = pipe_eof(source);

	if(eof_rc == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot check if the pipe has more data");

	if(eof_rc) return 0;

	if(ERROR_CODE(int) == _ensure_header_read(inst, source, model->type_info[i].used_size))
	    ERROR_RETURN_LOG(int, "Cannot read the typed header from the source");

	const _header_buf_t* src_buffer = (const _header_buf_t*)(inst->buffer + inst->model->type_info[PIPE_GET_ID(source)].buf_begin);
	_header_buf_t* dst_buffer = (_header_buf_t*)(inst->buffer + inst->model->type_info[i].buf_begin);
	const char* data;

	if(src_buffer->valid_size == ERROR_CODE(size_t))
	    data = src_buffer->bufptr[0];
	else
	    data = src_buffer->data;

	memcpy(dst_buffer->data, data, model->type_info[i].used_size);
	dst_buffer->valid_size = model->type_info[i].used_size;

	return 1;
}


/**
 * @brief Ensure the pipe header written properly. This means it will make sure all the bytes in [0, nbytes) must be
 *        properly initialized
 * @param inst The type instance
 * @param pipe the pipe we want to ensure
 * @param nbytes how many bytes we need to ensure
 * @return status code
 **/
static inline int _ensure_header_write(pstd_type_instance_t* inst, pipe_t pipe, size_t nbytes)
{
	const _typeinfo_t* typeinfo = inst->model->type_info + PIPE_GET_ID(pipe);
	_header_buf_t* buffer = (_header_buf_t*)(inst->buffer + typeinfo->buf_begin);
	if(nbytes <= buffer->valid_size) return 0;

	if(typeinfo->copy_from != ERROR_CODE(pipe_t) && buffer->valid_size == 0)
	{
		int copy_rc;
		if(ERROR_CODE(int) == (copy_rc = _copy_header_data(inst, pipe)))
		   ERROR_RETURN_LOG(int, "Cannot copy data from the source pipe");
		if(copy_rc)
		    return 0;
	}

	size_t bytes_to_fill = nbytes - buffer->valid_size;

	memset(buffer->data + buffer->valid_size, 0, bytes_to_fill);

	buffer->valid_size = nbytes;

	return 0;
}

int pstd_type_instance_write(pstd_type_instance_t* inst, pstd_type_accessor_t accessor, const void* buf, size_t bufsize)
{
	if(NULL == inst || ERROR_CODE(pstd_type_accessor_t) == accessor || NULL == buf || accessor >= inst->model->accessor_cnt)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	const _accessor_t* obj = inst->model->accessor + accessor;

	if(!obj->init) return 0;

	if(bufsize > obj->size) bufsize = obj->size;

	if(bufsize == 0) return 0;

	if(ERROR_CODE(int) == _ensure_header_write(inst, obj->pipe, obj->offset + bufsize))
	    ERROR_RETURN_LOG(int, "Cannot ensure the header buffer is valid");

	_header_buf_t* buffer = (_header_buf_t*)(inst->buffer + inst->model->type_info[PIPE_GET_ID(obj->pipe)].buf_begin);
	memcpy(buffer->data + obj->offset, buf, bufsize);

	return 0;
}

pstd_type_model_t* pstd_type_model_batch_init(const pstd_type_model_init_param_t* params, size_t count, pstd_type_model_t* model, ...)
{
	pstd_type_model_t* ret = model == NULL ? pstd_type_model_new() : model;

	if(NULL == ret)
	    ERROR_PTR_RETURN_LOG("Cannot initialize the type model");

	size_t i;
	for(i = 0; i < count; i ++)
	{
		const pstd_type_model_init_param_t* current = params + i;

		if(current->is_constant)
		{
			if(ERROR_CODE(int) == pstd_type_model_const(ret, current->pipe, current->field_expr,
			                                            current->const_buf.signedness,
			                                            current->const_buf.floatpoint,
			                                            current->const_buf.target_addr,
			                                            current->const_buf.const_size))
			    ERROR_LOG_GOTO(ERR, "Cannot initailize the constant field %s at %s:%u", current->pipe_name, current->filename, current->line);
		}
		else
		{
			if(NULL == current->accessor_buf)
			    ERROR_LOG_GOTO(ERR, "Invalid arguments for field accessor %s at %s:%u", current->pipe_name, current->filename, current->line);

			if(ERROR_CODE(pstd_type_accessor_t) == (*current->accessor_buf = pstd_type_model_get_accessor(ret, current->pipe, current->field_expr)))
			    ERROR_LOG_GOTO(ERR, "Cannot initailize the type accessor for field %s at %s:%u", current->pipe_name, current->filename, current->line);
		}
	}

	return ret;
ERR:
	pstd_type_model_free(ret);
	return NULL;
}
