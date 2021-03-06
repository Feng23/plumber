/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The dicitonary type
 * @note The dicitonary type is used as the global storage as well
 * @file pss/include/pss/dict.h
 **/
#ifndef __PSS_DICT_H__
#define __PSS_DICT_H__
/**
 * @brief The data type for the global storage
 **/

typedef struct _pss_dict_t pss_dict_t;

/**
 * @brief Register the type operations to the VM sytstem
 * @return status code
 **/
int pss_dict_init(void);

/**
 * @brief Reserved for some cleanup work
 * @return status code
 **/
int pss_dict_finalize(void);

/**
 * @brief Create a new dictionary
 * @return The newly created global storage
 **/
pss_dict_t* pss_dict_new(void);

/**
 * @brief Dispose a used ditionary
 * @param dict The global storage to dipsose
 * @return status code
 **/
int pss_dict_free(pss_dict_t* dict);

/**
 * @brief Get the variable from the dictionary
 * @param dict The dictionary
 * @param key The key we are looking for
 * @return The const value reference to the value we want to find, PSS_VALUE_KIND_ERROR when
 *         error cases, and PSS_VALUE_KIND_UNDEF for the undefined key
 **/
pss_value_t pss_dict_get(const pss_dict_t* dict, const char* key);

/**
 * @brief Set variable in the global storage
 * @param dict The dictionary
 * @param key The key to the variable
 * @param value The value we want to write
 * @return status code
 **/
int pss_dict_set(pss_dict_t* dict, const char* key, pss_value_t value);

/**
 * @brief Get the size of the dictionary
 * @param dict The target dictionary
 * @return The size
 **/
uint32_t pss_dict_size(const pss_dict_t* dict);

/**
 * @brief Get the n-th key in the dicionary
 * @param dict The target dictionary
 * @param i The i-th key
 * @return The key value
 **/
const char* pss_dict_get_key(const pss_dict_t* dict, uint32_t i);
#endif

