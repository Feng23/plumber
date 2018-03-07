/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#include <mime.h>
#include <options.h>
#include <input.h>
#include <file.h>
#include <http.h>
#include <raw.h>

/**
 * @brief The servlet context
 **/
typedef struct {
	options_t          options;       /*!< The servlet options */
	pstd_type_model_t* type_model;    /*!< The type model */

	input_ctx_t*       input_ctx;     /*!< The input context */

	union {
		file_ctx_t*    file_ctx;      /*!< The file context */
		http_ctx_t*    http_ctx;      /*!< The HTTP context */
		raw_ctx_t*     raw_ctx;       /*!< The RAW context */
	};
} ctx_t;

static int _init(uint32_t argc, char const* const* argv, void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	if(ERROR_CODE(int) == options_parse(argc, argv, &ctx->options))
		ERROR_RETURN_LOG(int, "Cannot parse the servlet init string");

	return 0;
}

static int _unload(void* ctxmem)
{
	ctx_t* ctx = (ctx_t*)ctxmem;

	int rc = 0;

	if(ERROR_CODE(int) == options_free(&ctx->options))
		rc = ERROR_CODE(int);

	return rc;
}

SERVLET_DEF = {
	.desc    = "Reads a file from disk",
	.version = 0x0,
	.size    = sizeof(ctx_t),
	.init    = _init,
	.unload  = _unload
};

