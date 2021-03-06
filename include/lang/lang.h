/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The high level include file for the service definition language (SDL)
 * @file lang/lang.h
 **/
#ifndef __PLUMBER_LANG_H__
#define __PLUMBER_LANG_H__

#include <lang/service.h>
#include <lang/prop.h>

/**
 * @brief initialize the SDL module
 * @return status code
 **/
int lang_init(void);

/**
 * @brief fianlize the SDL module
 * @return status code
 **/
int lang_finalize(void);


#endif /*__PLUMBER_LANG_H__*/
