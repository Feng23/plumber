/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <pservlet.h>
#include <pstd.h>
#include <proto.h>

#include <pstd/types/string.h>

typedef struct {
	uint32_t prefix_level;   /*!< How many level of directory are considered to be prefix */
	uint32_t need_extname:1; /*!< Do we need the extension name? */
	pipe_t origin;           /*!< The original input pipe */
	pipe_t prefix;           /*!< The pipe outputs the prefix */
	pipe_t relative;         /*!< The relative path */
	pipe_t extname;          /*!< The extension name of the path, only valid when the extension name mode is open */

	pstd_type_model_t*    model;          /*!< The type model we used */
	pstd_type_accessor_t  origin_token;   /*!< The origin token */
	pstd_type_accessor_t  relative_token; /*!< The relative token */
	pstd_type_accessor_t  prefix_token;   /*!< The prefix token */
	pstd_type_accessor_t  extname_token;  /*!< The externsion name token */
} context_t;

static inline int _write_string(pstd_type_instance_t* inst, pstd_type_accessor_t accessor, pstd_string_t* str)
{
	 scope_token_t token;
	 if(ERROR_CODE(scope_token_t) == (token = pstd_string_commit(str)))
	     ERROR_LOG_GOTO(ERR, "Cannot commit string object to RLS");

	 return PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, accessor, token);
ERR:
	 pstd_string_free(str);
	 return ERROR_CODE(int);;
}

static inline int _write_path(pstd_type_instance_t* inst, pstd_type_accessor_t accessor, char const* const* begin, char const* const* end, uint32_t n)
{
	pstd_string_t* result = pstd_string_new(128);
	if(NULL == result) ERROR_RETURN_LOG(int, "Cannot create new string object for the path");

	if(n == 0 && ERROR_CODE(size_t) == pstd_string_write(result, "/", 1))
	    ERROR_RETURN_LOG(int, "RLS string write error");

	for(; n > 0; n --, begin ++, end ++)
	    if(ERROR_CODE(size_t) == pstd_string_write(result, "/", 1) ||
	       ERROR_CODE(size_t) == pstd_string_write(result, begin[0], (size_t)(end[0] - begin[0])))
	        ERROR_RETURN_LOG(int, "RLS string write error");

	return _write_string(inst, accessor, result);
}

static inline int _simplify_path(scope_token_t path_token, const context_t* ctx, pstd_type_instance_t* inst)
{
	const pstd_string_t* pathstr = pstd_string_from_rls(path_token);
	if(NULL == pathstr) ERROR_RETURN_LOG(int, "Cannot get string object from RLS");

	const char* path = pstd_string_value(pathstr);
	if(NULL == path) ERROR_RETURN_LOG(int, "Cannot get the value of the string");

	/* The reason why we only have PATH_MAX / 2 segments is because we define the segment as
	 * *non-empty* string seperated by slash '/'. Which means we *must* have at least one char
	 * otehr than '/'. Thus, if the maximum length of path allowed is PATH_MAX, the maximum number
	 * of segment is less than PATH_MAX / 2 */
	const char *bs[PATH_MAX / 2 + 1], *es[PATH_MAX / 2 + 1];
	const char *begin = path + (path[0] == '/'), *end = path + (path[0] == '/');
	const char *extname = NULL;
	int32_t sp = 0, simplified = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-overflow"
	/* Since it's unlikely that we can have sp overflowed, thus we are able to ignore that*/
	for(;sp >= 0;end++)
	    if(*end == '/' || *end == 0)
	    {
		    if(end - begin == 2 && begin[0] == '.' && begin[1] == '.')  /* Pop the stack when the segment is .. */
		        sp --, simplified = 1;
		    else if((end - begin == 1 && begin[0] == '.') || (end - begin == 0)) /* Silently swallow the segment if the segment is empty or . */
		        simplified = 1;  /* TODO: We have a flaw here, because the path / will be marked simplified but output is identical, Nothing to do with the correctness */
		    else /* Otherwise push everything to stack */
		        bs[sp] = begin, es[sp] = end, sp ++;

		    begin = end + 1;
		    if(*end == 0) break;
		    else extname = NULL;
	    }
	    else if(ctx->need_extname && *end == '.') extname = end + 1;

	/* If we pop the stack too much, this should not be allowed, so we produce zero output */
	if(sp < 0) return 0;
#pragma GCC diagnostic pop

	uint32_t nprefix = (ctx->prefix_level > (uint32_t)sp) ? 0 : ctx->prefix_level;
	if(ctx->prefix_token != ERROR_CODE(pstd_type_accessor_t) && ERROR_CODE(int) == _write_path(inst, ctx->prefix_token, bs, es, nprefix))
	    ERROR_RETURN_LOG(int, "Cannot write the path to pipe");

	if(nprefix == 0 && !simplified)
	{
		/* If the path haven't been changed, we can reuse the string directly */
		if(ERROR_CODE(int) == PSTD_TYPE_INST_WRITE_PRIMITIVE(inst, ctx->relative_token, path_token))
		    ERROR_RETURN_LOG(int, "Cannot write the relative path to pipe");
	}
	else if(ERROR_CODE(int) == _write_path(inst, ctx->relative_token, bs + nprefix, es + nprefix, (uint32_t)(sp) - nprefix))
	    ERROR_RETURN_LOG(int, "Cannot write the relative path to pipe");

	if(NULL != extname)
	{
		pstd_string_t* buf = pstd_string_new(16);
		if(ERROR_CODE(size_t) == pstd_string_write(buf, extname, strlen(extname)))
		    ERROR_RETURN_LOG(int, "Cannot write extension name to string object");

		if(ERROR_CODE(int) == _write_string(inst, ctx->extname_token, buf))
		    ERROR_RETURN_LOG(int, "Cannot write the extension name to the pipe");
	}

	return 0;
}

static int _set_options(pstd_option_data_t data)
{
	char what = data.current_option->short_opt;
	context_t* ctx = (context_t*)data.cb_data;

	switch(what)
	{
		case 'L':
		    if(data.param_array[0].intval < 0 || data.param_array[0].intval > PATH_MAX / 2 + 1)
		        ERROR_RETURN_LOG(int, "Invalid prefix level %"PRId64, data.param_array[0].intval);
		    ctx->prefix_level = (uint32_t)data.param_array[0].intval;
		    return 0;
		case 'e':
		    ctx->need_extname = 1;
		    return 0;
		default:
		    ERROR_RETURN_LOG(int, "Invalid option %c", what);
	}
}

static int _init(uint32_t argc, char const* const* argv, void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	ctx->prefix_level = 0;
	ctx->need_extname = 0;

	static pstd_option_t opts[] = {
		{
			.long_opt     = "prefix-level",
			.short_opt    = 'L',
			.description  = "How many levels of directories are considered prefix",
			.pattern      = "I",
			.handler      = _set_options
		},
		{
			.long_opt     = "ext-name",
			.short_opt    = 'e',
			.description  = "Produce the extension name to the extension name pipe",
			.pattern      = "",
			.handler      = _set_options
		},
		{
			.long_opt     = "help",
			.short_opt    = 'h',
			.description  = "Print this help message",
			.pattern      = "",
			.handler      = pstd_option_handler_print_help
		}
	};

	uint32_t opt_rc = pstd_option_parse(opts, sizeof(opts) / sizeof(opts[0]), argc, argv, ctx);

	static const char* strtype = "plumber/std/request_local/String";

	if(opt_rc !=  argc)
	    ERROR_RETURN_LOG(int, "Invalid servlet initialization string, use --help for the usage of the servlet");

	if(ERROR_CODE(pipe_t) == (ctx->origin = pipe_define("origin", PIPE_INPUT, strtype)))
	    ERROR_RETURN_LOG(int, "Cannot define the input pipe for original path");

	if(ERROR_CODE(pipe_t) == (ctx->prefix = pipe_define("prefix", PIPE_OUTPUT, strtype)))
	    ERROR_RETURN_LOG(int, "Cannot define the output pipe for the prefix");

	if(ERROR_CODE(pipe_t) == (ctx->relative = pipe_define("relative", PIPE_OUTPUT, strtype)))
	    ERROR_RETURN_LOG(int, "Cannot define the output pipe for the relative path");

	if(ctx->need_extname && ERROR_CODE(pipe_t) == (ctx->extname = pipe_define("extname", PIPE_OUTPUT, strtype)))
	    ERROR_RETURN_LOG(int, "Cannot define the output pipe for the externsion name");

	if(NULL == (ctx->model = pstd_type_model_new()))
	    ERROR_RETURN_LOG(int, "Cannot create type model");

	ctx->prefix_token = ERROR_CODE(pstd_type_accessor_t);

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->prefix_token = pstd_type_model_get_accessor(ctx->model, ctx->prefix, "token")))
	    ERROR_RETURN_LOG(int, "Cannot get accessor for field prefix.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->relative_token = pstd_type_model_get_accessor(ctx->model, ctx->relative, "token")))
	    ERROR_RETURN_LOG(int, "Cannot get accessor for field relative.token");

	if(ctx->need_extname && ERROR_CODE(pstd_type_accessor_t) == (ctx->extname_token = pstd_type_model_get_accessor(ctx->model, ctx->extname, "token")))
	    ERROR_RETURN_LOG(int, "Cannot get the accessor for field extname.token");

	if(ERROR_CODE(pstd_type_accessor_t) == (ctx->origin_token = pstd_type_model_get_accessor(ctx->model, ctx->origin, "token")))
	    ERROR_RETURN_LOG(int, "Cannot get the accesor for field origin.token");

	return 0;
}

static int _exec(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	pstd_type_instance_t* inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->model);
	if(NULL == inst)
	    ERROR_LOG_GOTO(ERR, "Cannot create the model");

	scope_token_t token;
	if(ERROR_CODE(scope_token_t) == (token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->origin_token)))
	    ERROR_LOG_GOTO(ERR, "Cannot read the RLS token from input");

	if(ERROR_CODE(int) == _simplify_path(token, ctx, inst))
	    ERROR_LOG_GOTO(ERR, "Cannot simplify the path");

	return pstd_type_instance_free(inst);

ERR:
	if(NULL != inst) pstd_type_instance_free(inst);

	return ERROR_CODE(int);
}

static inline int _unload(void* ctxbuf)
{
	context_t* ctx = (context_t*)ctxbuf;

	return pstd_type_model_free(ctx->model);
}

SERVLET_DEF = {
	.desc = "Parse a HTTP path from the input pipe and output the prefix and relative path as string.",
	.version = 0x0,
	.size = sizeof(context_t),
	.init = _init,
	.exec = _exec,
	.unload = _unload
};
