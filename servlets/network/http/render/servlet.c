/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <pstd/types/string.h>
#include <pstd/types/file.h>

#include <options.h>
#include <zlib_token.h>
#include <chunked.h>

enum {
	_ENCODING_GZIP    = 1,
	_ENCODING_DEFLATE = 2,
	_ENCODING_BR      = 4,
	_ENCODING_CHUNKED = 8,
	_ENCODING_COMPRESSED = 7
};

/**
 * @brief The servlet context
 **/
typedef struct {
	options_t            opts;               /*!< The servlet options */
	pipe_t               p_response;         /*!< The pipe for the response to render */
	pipe_t               p_proxy;            /*!< The reverse proxy response */
	pipe_t               p_accept_encoding;  /*!< The accept-encoding */
	pipe_t               p_500;              /*!< The 500 status signal pipe */
	pipe_t               p_output;           /*!< The output port */

	pstd_type_model_t*   type_model;         /*!< The type model for this servlet */
	pstd_type_accessor_t a_status_code;      /*!< The accessor for the status_code */
	pstd_type_accessor_t a_body_flags;       /*!< The accessor for body flags */
	pstd_type_accessor_t a_body_size;        /*!< The accessor for the body size */
	pstd_type_accessor_t a_body_token;       /*!< The accessor for RLS token */
	pstd_type_accessor_t a_mime_type;        /*!< The MIME type RLS token */
	pstd_type_accessor_t a_redir_loc;        /*!< The redirect location RLS token accessor */
	pstd_type_accessor_t a_accept_enc;       /*!< The accept encoding RLS token */
	pstd_type_accessor_t a_proxy_token;      /*!< The reverse proxy token */

	uint32_t             BODY_SIZE_UNKNOWN;  /*!< The unknown body size */
	uint32_t             BODY_CAN_COMPRESS;  /*!< Indicates if the body should be compressed */
} ctx_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->opts))
		ERROR_RETURN_LOG(int, "Cannot parse the servlet init string");

	PIPE_LIST(pipes) 
	{
		PIPE("response",        PIPE_INPUT,               "plumber/std_servlet/network/http/render/v0/Response", ctx->p_response),
		PIPE("accept_encoding", PIPE_INPUT,               "plumber/std/request_local/String",                    ctx->p_accept_encoding),
		PIPE("500",             PIPE_INPUT,               NULL,                                                  ctx->p_500),
		PIPE("output",          PIPE_OUTPUT | PIPE_ASYNC, NULL,                                                  ctx->p_output)
	};

	if(ERROR_CODE(int) == PIPE_BATCH_INIT(pipes)) return ERROR_CODE(int);

	PSTD_TYPE_MODEL(model_list) 
	{
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        status.status_code,       ctx->a_status_code),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        body_flags,               ctx->a_body_token),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        body_size,                ctx->a_body_size),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        mime_type.token,          ctx->a_mime_type),
		PSTD_TYPE_MODEL_FIELD(ctx->p_response,        redirect_location.token,  ctx->a_redir_loc),
		PSTD_TYPE_MODEL_FIELD(ctx->p_accept_encoding, token,                    ctx->a_accept_enc),
		PSTD_TYPE_MODEL_CONST(ctx->p_response,        BODY_SIZE_UNKNOWN,        ctx->BODY_SIZE_UNKNOWN),
		PSTD_TYPE_MODEL_CONST(ctx->p_response,        BODY_CAN_COMPRESS,        ctx->BODY_CAN_COMPRESS)
	};

	if(NULL == (ctx->type_model = PSTD_TYPE_MODEL_BATCH_INIT(model_list))) return ERROR_CODE(int);

	if(ctx->opts.reverse_proxy)
	{
		if(ERROR_CODE(pipe_t) == (ctx->p_proxy = pipe_define("proxy", PIPE_INPUT, "plumber/std_servlet/network/http/proxy/v0/Response")))
			ERROR_RETURN_LOG(int, "Cannot declare the proxy pipe");

		if(ERROR_CODE(pstd_type_accessor_t) == (ctx->a_proxy_token = pstd_type_model_get_accessor(ctx->type_model, ctx->p_proxy, "token")))
			ERROR_RETURN_LOG(int, "Cannot get the accessor for proxy.token");
	}

	return 0;
}

static int _unload(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	int rc = 0;

	if(ERROR_CODE(int) == options_free(&ctx->opts))
		rc = ERROR_CODE(int);

	if(ERROR_CODE(int) == pstd_type_model_free(ctx->type_model))
		rc = ERROR_CODE(int);

	return rc;
}

static inline int _write_status_line(pstd_bio_t* bio, uint16_t status_code)
{
	const char* status_phrase = "Unknown Status Phrase";

#define STATUS_PHRASE(code, text) case code: status_phrase = text; break

	switch(status_code)
	{
		STATUS_PHRASE(100,"Continue");
		STATUS_PHRASE(101,"Switching Protocols");
		STATUS_PHRASE(102,"Processing");
		STATUS_PHRASE(103,"Early Hints");
		STATUS_PHRASE(200,"OK");
		STATUS_PHRASE(201,"Created");
		STATUS_PHRASE(202,"Accepted");
		STATUS_PHRASE(203,"Non-Authoritative Information");
		STATUS_PHRASE(204,"No Content");
		STATUS_PHRASE(205,"Reset Content");
		STATUS_PHRASE(206,"Partial Content");
		STATUS_PHRASE(207,"Multi-Status");
		STATUS_PHRASE(208,"Already Reported");
		STATUS_PHRASE(226,"IM Used");
		STATUS_PHRASE(300,"Multiple Choices");
		STATUS_PHRASE(301,"Moved Permanently");
		STATUS_PHRASE(302,"Found");
		STATUS_PHRASE(303,"See Other");
		STATUS_PHRASE(304,"Not Modified");
		STATUS_PHRASE(305,"Use Proxy");
		STATUS_PHRASE(306,"Switch Proxy");
		STATUS_PHRASE(307,"Temporary Redirect");
		STATUS_PHRASE(308,"Permanent Redirect");
		STATUS_PHRASE(400,"Bad Request");
		STATUS_PHRASE(401,"Unauthorized");
		STATUS_PHRASE(402,"Payment Required");
		STATUS_PHRASE(403,"Forbidden");
		STATUS_PHRASE(404,"Not Found");
		STATUS_PHRASE(405,"Method Not Allowed");
		STATUS_PHRASE(406,"Not Acceptable");
		STATUS_PHRASE(407,"Proxy Authentication Required");
		STATUS_PHRASE(408,"Request Timeout");
		STATUS_PHRASE(409,"Conflict");
		STATUS_PHRASE(410,"Gone");
		STATUS_PHRASE(411,"Length Required");
		STATUS_PHRASE(412,"Precondition Failed");
		STATUS_PHRASE(413,"Payload Too Large");
		STATUS_PHRASE(414,"URI Too Long");
		STATUS_PHRASE(415,"Unsupported Media Type");
		STATUS_PHRASE(416,"Range Not Satisfiable");
		STATUS_PHRASE(417,"Expectation Failed");
		STATUS_PHRASE(418,"I'm a teapot");
		STATUS_PHRASE(421,"Misdirected Request");
		STATUS_PHRASE(422,"Unprocessable Entity");
		STATUS_PHRASE(423,"Locked");
		STATUS_PHRASE(424,"Failed Dependency");
		STATUS_PHRASE(426,"Upgrade Required");
		STATUS_PHRASE(428,"Precondition Required");
		STATUS_PHRASE(429,"Too Many Requests");
		STATUS_PHRASE(431,"Request Header Fields Too Large");
		STATUS_PHRASE(451,"Unavailable For Legal Reasons");
		STATUS_PHRASE(500,"Internal Server Error");
		STATUS_PHRASE(501,"Not Implemented");
		STATUS_PHRASE(502,"Bad Gateway");
		STATUS_PHRASE(503,"Service Unavailable");
		STATUS_PHRASE(504,"Gateway Timeout");
		STATUS_PHRASE(505,"HTTP Version Not Supported");
		STATUS_PHRASE(506,"Variant Also Negotiates");
		STATUS_PHRASE(507,"Insufficient Storage");
		STATUS_PHRASE(508,"Loop Detected");
		STATUS_PHRASE(510,"Not Extended");
		STATUS_PHRASE(511,"Network Authentication Required");
		default:
			(void)0;
	}

	if(ERROR_CODE(size_t) == pstd_bio_printf(bio, "HTTP/1.1 %d %s\r\n", status_code, status_phrase))
		ERROR_RETURN_LOG(int, "Cannot write the status line");

	return 0;
}

static inline int _write_string_field(pstd_bio_t* bio, pstd_type_instance_t* inst, pstd_type_accessor_t mime_acc, const char* name, const char* defval)
{
	scope_token_t tok = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, mime_acc);

	if(ERROR_CODE(scope_token_t) == tok)
		ERROR_RETURN_LOG(int, "Cannot read response.mime_type.token from pipe response");

	const char* value = defval;

	if(tok != 0)
	{
		const pstd_string_t* str_rls = pstd_string_from_rls(tok);
		if(NULL == str_rls)
			ERROR_RETURN_LOG(int, "Cannot acquire the string RLS object from the scope");

		if(NULL == (value = pstd_string_value(str_rls)))
			ERROR_RETURN_LOG(int, "Cannot get the string pointer for the string RLS object");
	}

	if(ERROR_CODE(size_t) == pstd_bio_printf(bio, "%s: %s\r\n", name, value))
		ERROR_RETURN_LOG(int, "Cannot write the %s field", name);

	return 0;
}

static inline uint32_t _determine_compression_algorithm(const ctx_t *ctx, pstd_type_instance_t* inst, int compress_enabled)
{
	if(pipe_eof(ctx->p_accept_encoding) == 1)
	    return 0;

	scope_token_t accept_token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, inst, ctx->a_accept_enc);

	if(ERROR_CODE(scope_token_t) == accept_token)
	    ERROR_RETURN_LOG(uint32_t, "Cannot read the accept token from pipe");

	const pstd_string_t* accept_obj = pstd_string_from_rls(accept_token);

	if(NULL == accept_obj)
	    ERROR_RETURN_LOG(uint32_t, "Cannot get the RLS string object for the token");

	const char* accepts = pstd_string_value(accept_obj);
	const char* accepts_end = accepts + pstd_string_length(accept_obj);

	if(NULL == accepts)
	    ERROR_RETURN_LOG(uint32_t, "Cannot get the accepts string");

	unsigned current_len = 0;
	const char* ptr;
	uint32_t ret = 0;
	uint32_t compressed = !compress_enabled || !(ctx->opts.gzip_enabled || ctx->opts.deflate_enabled || ctx->opts.br_enabled);
	for(ptr = accepts; ptr < accepts_end && !compressed; ptr ++)
	{
		if(current_len == 0)
		{
			if(*ptr == ' ' || *ptr == '\t')
			    continue;
			else
			{
				switch(compressed ? -1 : *ptr)
				{
#ifdef HAS_ZLIB
					case 'g':
					    /* gzip */
					    if(ctx->opts.gzip_enabled && accepts_end - ptr >= 4 && memcmp("gzip", ptr, 4) == 0)
					         ret |= _ENCODING_GZIP, compressed = 1;
					    break;
					case 'd':
					    /* deflate */
					    if(ctx->opts.deflate_enabled && accepts_end - ptr >= 7 && memcmp("deflate", ptr, 7) == 0)
					        ret |= _ENCODING_DEFLATE, compressed = 1;
					    break;
#endif
#ifdef HAS_BROTLI
					case 'b':
					    /* br */
					    if(ctx->opts.br_enabled && accepts_end - ptr >= 2 && memcmp("br", ptr, 2) == 0)
					        ret |= _ENCODING_BR, compressed = 1;
					    break;
#endif
					default:
					    if(NULL == (ptr = strchr(ptr, ',')))
					        return ret;
				}
			}
		}
	}

	if(ret > 0) ret |= _ENCODING_CHUNKED;
	return ret;
}

static inline int _write_encoding(pstd_bio_t* bio, uint32_t algorithm, uint64_t size)
{
	if((algorithm & _ENCODING_COMPRESSED))
	{
		const char* algorithm_name = "identity";
		if((algorithm & _ENCODING_GZIP))
			algorithm_name = "gzip";
		else if((algorithm & _ENCODING_DEFLATE))
			algorithm_name = "deflate";
#ifdef HAS_BROTLI
		else if((algorithm & _ENCODING_BR))
			algorithm_name = "br";
#endif
		if(ERROR_CODE(size_t) == pstd_bio_printf(bio, "Content-Encoding: %s\r\n", algorithm_name))
			ERROR_RETURN_LOG(int, "Cannot write the Content-Encoding header");
	}

	if((algorithm & _ENCODING_CHUNKED))
	{
		if(ERROR_CODE(size_t) == pstd_bio_puts(bio, "Transfer-Encoding: chunked\r\n"))
			ERROR_RETURN_LOG(int, "Cannot write the Transfer-Encoding header");
	}
	else
	{
		if(ERROR_CODE(size_t) == pstd_bio_printf(bio, "Content-Length: %zu\r\n", size))
			ERROR_RETURN_LOG(int, "Cannot write the Content-Length header");
	}

	return 0;
}

static inline scope_token_t _write_error_page(pstd_bio_t* bio, uint16_t status, const options_error_page_t* page, const char* default_page)
{
	if(ERROR_CODE(int) == _write_status_line(bio, status))
		ERROR_RETURN_LOG(scope_token_t, "Cannot write the status line");

	size_t length;
	scope_token_t ret = ERROR_CODE(scope_token_t);

	if(NULL == page->error_page)
	{
DEF_ERR_PAGE:
		length = strlen(default_page);

		pstd_string_t* str_rls = pstd_string_from_onwership_pointer(strdup(default_page), length);

		if(NULL == str_rls)
			ERROR_RETURN_LOG(scope_token_t, "Cannot create the default error page");

		if(ERROR_CODE(scope_token_t) == (ret = pstd_string_commit(str_rls)))
		{
			pstd_string_free(str_rls);
			ERROR_RETURN_LOG(scope_token_t, "Cannot commit the RLS string to the RLS");
		}
	}
	else
	{
		pstd_file_t* err_page = pstd_file_new(page->error_page);
		if(NULL == err_page)
			ERROR_RETURN_LOG(scope_token_t, "Cannot create RLS file object for the error page");

		int exist = pstd_file_exist(err_page);
		if(exist == ERROR_CODE(int))
			ERROR_LOG_GOTO(ERR, "Cannot check if the error page exists");

		if(!exist) goto DEF_ERR_PAGE;
		
		if(ERROR_CODE(size_t) == (length = pstd_file_size(err_page)))
			ERROR_LOG_GOTO(ERR, "Cannot get the size of the error page");

		if(ERROR_CODE(scope_token_t) == (ret = pstd_file_commit(err_page)))
			ERROR_LOG_GOTO(ERR, "Cannot commit the RLS file object to scope");

		goto WRITE;
ERR:
		pstd_file_free(err_page);
		return ERROR_CODE(scope_token_t);
	}

WRITE:

	if(ERROR_CODE(size_t) == pstd_bio_printf(bio, "Content-Type: %s\r\n"
				                               "Content-Length: %zu\r\n\r\n", page->mime_type, (size_t)length))
		ERROR_RETURN_LOG(scope_token_t, "Cannot write the header");

	return ret;
}


static inline int _write_connection_field(pstd_bio_t* out, pipe_t res, int needs_close)
{
	pipe_flags_t flags = 0;

	if(needs_close == 0)
	{
		if(ERROR_CODE(int) == pipe_cntl(res, PIPE_CNTL_GET_FLAGS, &flags))
			ERROR_RETURN_LOG(int, "Cannot get the pipe flags");
	}
	else
	{
		if(ERROR_CODE(int) == pipe_cntl(res, PIPE_CNTL_CLR_FLAG, PIPE_PERSIST))
			ERROR_RETURN_LOG(int, "Cannot clear the persistent flag");
	}

	if((flags & PIPE_PERSIST))
	{
	    if(ERROR_CODE(size_t) == pstd_bio_printf(out, "Connection: keep-alive\r\n"))
			ERROR_RETURN_LOG(int, "Cannot write the connection field");
	}
	else
	{
	    if(ERROR_CODE(size_t) == pstd_bio_printf(out, "Connection: close\r\n"))
			ERROR_RETURN_LOG(int, "Cannot write the connection field");
	}

	return 0;
}

static int _exec(void* ctxmem)
{
	uint16_t status_code;
	uint32_t body_flags, algorithm;
	uint64_t body_size = ERROR_CODE(uint64_t);
	scope_token_t body_token;
	int eof_rc;

	pstd_bio_t* out = NULL; 
	ctx_t* ctx = (ctx_t*)ctxmem;
	pstd_type_instance_t* type_inst = PSTD_TYPE_INSTANCE_LOCAL_NEW(ctx->type_model);

	if(NULL == type_inst)
		ERROR_RETURN_LOG(int, "Cannot create type instance for the servlet");

	if(NULL == (out = pstd_bio_new(ctx->p_output)))
		ERROR_LOG_GOTO(ERR, "Cannot create new pstd BIO object for the output pipe");

	/* Check if we need a HTTP 500 */
	if(ERROR_CODE(int) == (eof_rc = pipe_eof(ctx->p_500)))
		ERROR_LOG_GOTO(ERR, "Cannot check if we got service internal error signal");

	if(!eof_rc)
	{
		const char* default_500 = "<html><body><h1>Server Internal Error</h1></body></html>";
		if(ERROR_CODE(scope_token_t) == (body_token = _write_error_page(out, 500, &ctx->opts.err_500, default_500)))
			ERROR_LOG_GOTO(ERR, "Cannot write the HTTP 500 response");

		if(ERROR_CODE(int) == _write_connection_field(out, ctx->p_output, 0))
			ERROR_LOG_GOTO(ERR, "Cannot write the connection field");

		goto RET;
	}

	/* Step0: Check if we got a proxy response */
	if(ctx->opts.reverse_proxy)
	{
		int has_proxy;

		if(ERROR_CODE(int) == (has_proxy = pipe_eof(ctx->p_proxy)))
			ERROR_LOG_GOTO(ERR, "Cannot check if we have reverse proxy response");

		if(has_proxy)
		{
			scope_token_t scope = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, type_inst, ctx->a_proxy_token);
			if(ERROR_CODE(int) == pstd_bio_write_scope_token(out, scope))
			{
				const char* default_503 = "<html><body><h1>Service Unavailable</h1></body></html>";
				if(ERROR_CODE(scope_token_t) == (body_token = _write_error_page(out, 503, &ctx->opts.err_503, default_503)))
					ERROR_LOG_GOTO(ERR, "Cannot write HTTP 503 response");

				if(ERROR_CODE(int) == _write_connection_field(out, ctx->p_output, 0))
					ERROR_LOG_GOTO(ERR, "Cannot write the connection field");

				goto RET;
			}
		}
		else goto PROXY_RET; 
	}

	/* Step1: Dtermine the encoding algorithm, size etc... */
	
	if(ERROR_CODE(uint32_t) == (body_flags = PSTD_TYPE_INST_READ_PRIMITIVE(uint32_t, type_inst, ctx->a_body_flags)))
		ERROR_LOG_GOTO(ERR, "Cannot read the body flag");

	if(ERROR_CODE(uint32_t) == (algorithm = _determine_compression_algorithm(ctx, type_inst, (body_flags & ctx->BODY_CAN_COMPRESS) > 0)))
		ERROR_LOG_GOTO(ERR, "Cannot determine the encoding algorithm");

	if(ERROR_CODE(scope_token_t) == (body_token = PSTD_TYPE_INST_READ_PRIMITIVE(scope_token_t, type_inst, ctx->a_body_token)))
		ERROR_LOG_GOTO(ERR, "Cannot get the request body RLS token");
	if(0);
#ifdef HAS_ZLIB
	else if((algorithm & _ENCODING_GZIP))
	{
		if(ERROR_CODE(scope_token_t) == (body_token = zlib_token_encode(body_token, ZLIB_TOKEN_FORMAT_GZIP, ctx->opts.compress_level)))
			ERROR_LOG_GOTO(ERR, "Cannot encode the body with GZIP encoder");
		else
			body_flags |= ctx->BODY_SIZE_UNKNOWN;
	}
	else if((algorithm & _ENCODING_DEFLATE))
	{
		if(ERROR_CODE(scope_token_t) == (body_token = zlib_token_encode(body_token, ZLIB_TOKEN_FORMAT_DEFLATE, ctx->opts.compress_level)))
			ERROR_LOG_GOTO(ERR, "Cannot encode the body with Deflate encoder");
		else
			body_flags |= ctx->BODY_SIZE_UNKNOWN;
	}
#endif
#ifdef HAS_BROTLI
	else if((algorithm & _ENCODING_BR))
	{
		/* TODO: Brotli support */
	}
#endif

	if((algorithm & _ENCODING_CHUNKED))
	{
		if(ERROR_CODE(scope_token_t) == (body_token = chunked_encode(body_token, ctx->opts.max_chunk_size)))
			ERROR_LOG_GOTO(ERR, "Cannot encode body with chunked encoder");
		else
			body_flags |= ctx->BODY_SIZE_UNKNOWN;
	}

	if((body_flags & ctx->BODY_SIZE_UNKNOWN))
	{
		if(ERROR_CODE(uint64_t) == (body_size = PSTD_TYPE_INST_READ_PRIMITIVE(uint64_t, type_inst, ctx->a_body_size)))
			ERROR_LOG_GOTO(ERR, "Cannot determine the size of the body");
	}
	else if(!(algorithm & _ENCODING_CHUNKED))
	{
		const char* default_406 = "<html><body><h1>Content Encoding Not Acceptable</h1></body></html>";
		if(ERROR_CODE(scope_token_t) == (body_token = _write_error_page(out, 406, &ctx->opts.err_406, default_406)))
			ERROR_LOG_GOTO(ERR, "Cannot write the HTTP 500 response");

		if(ERROR_CODE(int) == _write_connection_field(out, ctx->p_output, 0))
			ERROR_LOG_GOTO(ERR, "Cannot write the connection field");

		goto RET;
	}


	/* Write the status line */

	if(ERROR_CODE(uint16_t) == (status_code = PSTD_TYPE_INST_READ_PRIMITIVE(uint16_t, type_inst, ctx->a_status_code)))
		ERROR_LOG_GOTO(ERR, "Cannot read the status code from response pipe");

	if(ERROR_CODE(int) == _write_status_line(out, status_code))
		ERROR_LOG_GOTO(ERR, "Cannot write the status code");


	/* Write the content type */
	if(ERROR_CODE(int) == _write_string_field(out, type_inst, ctx->a_mime_type, "Content-Type", "application/octet-stream" ))
		ERROR_LOG_GOTO(ERR, "Cannot write the mime type");

	/* Write redirections */
	if(status_code == 301 || status_code == 302 || status_code == 308 || status_code == 309)
	{
		if(ERROR_CODE(int) != _write_string_field(out, type_inst, ctx->a_redir_loc, "Location", "/"))
			ERROR_LOG_GOTO(ERR, "Cannot write the redirect location");
	}

	/* Write the encoding fields */
	if(ERROR_CODE(int) == _write_encoding(out, algorithm, body_size))
		ERROR_RETURN_LOG(int, "Cannot write the encoding fields");

	/* Write the connection field */
	if(ERROR_CODE(int) == _write_connection_field(out, ctx->p_output, 0))
		ERROR_LOG_GOTO(ERR, "Cannot write the connection field");

	/* Write the body deliminators */
	if(ERROR_CODE(size_t) == pstd_bio_puts(out, "\r\n"))
		ERROR_RETURN_LOG(int, "Cannot write the body deliminator");

RET:

	/* Write the server name */
	if(NULL != ctx->opts.server_name && ERROR_CODE(size_t) != pstd_bio_printf(out, "Server: %s\r\n", ctx->opts.server_name))
		ERROR_LOG_GOTO(ERR, "Cannot write the server name field");

	/* Write the body */
	if(ERROR_CODE(int) == pstd_bio_write_scope_token(out, body_token))
		ERROR_RETURN_LOG(int, "Cannot write the body content");

PROXY_RET:

	if(ERROR_CODE(int) == pstd_type_instance_free(type_inst))
		ERROR_RETURN_LOG(int, "Cannot dispose the type instance");

	if(ERROR_CODE(int) == pstd_bio_free(out))
		ERROR_RETURN_LOG(int, "Cannot dispose the BIO object");

	return 0;
ERR:
	if(NULL != type_inst) pstd_type_instance_free(type_inst);
	if(NULL != out) pstd_bio_free(out);
	
	return ERROR_CODE(int);
}

SERVLET_DEF = {
	.desc    = "HTTP Response Render",
	.version = 0x0,
	.size    = sizeof(ctx_t),
	.init    = _init,
	.unload  = _unload,
	.exec    = _exec
};