/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The runtime values of PSS VM
 * @file pss/include/pss/value.h
 **/

#include <utils/static_assertion.h>

#ifndef __PSS_VALUE_H__
#define __PSS_VALUE_H__
/**
 * @brief The callback functions used to manipulate a runtime value
 **/
typedef struct {
	/**
	 * @brief Make a new value from the given data pointer
	 * @param data The data pointer
	 * @note  This function is used when we want to convert a value to string.
	 *        And type_ops_string.mkval(str) will be called to make a new string
	 * @return The newly created data
	 **/
	void* (*mkval)(void* data);
	/**
	 * @brief Make a copy of the value
	 * @param value The value to copy
	 * @return The newly copied value
	 **/
	void* (*copy)(const void* value);  	
	/**
	 * @brief Dispose a unused value
	 * @param value The value to dispose
	 * @return status code
	 **/
	int   (*free)(void* value);
	/**
	 * @brief Convert the value to it's string representation
	 * @param value The value to convert
	 * @param buf The buffer for the string 
	 * @param bufsize The size of the buffer
	 * @return The result string
	 **/
	const char* (*tostr)(const void* value, char* buf, size_t bufsize);
} pss_value_ref_ops_t;

/**
 * @brief The enum indicates what type of value it is
 **/
typedef enum {
	PSS_VALUE_TYPE_DICT,    /*!< A dictionary */
	PSS_VALUE_TYPE_STRING,  /*!< A string */
	PSS_VALUE_TYPE_CLOSURE, /*!< A closure */
	PSS_VALUE_TYPE_TEST     /*!< The type reserved for test cases */
} pss_value_ref_type_t;

/**
 * @brief The data structure for a reference runtime value
 **/
typedef struct _pss_value_ref_t pss_value_ref_t;

/**
 * @brief A runtime value
 * @note  This is the mutable version of the runtime value. It means we are able to
 *        change the reference counter
 **/
typedef struct {
	uint8_t                        is_ref;     /*!< Indicates if this value is a reference */
	union {
		pss_value_ref_t*           ref;        /*!< A reference value */
		pss_bytecode_numeric_t     num;        /*!< A reference value */
	};
} pss_value_t;

/**
 * @brief A const runtime value
 * @note  This is the immutable version of runtime value. It means we don't have permission
 *        to change the reference counter. However, we are still have permission to change
 *        the value of it. <br/>
 *        In order to make a non-const version of the value, you should call copy function
 *        to make a duplication of the value
 **/
typedef struct {
	uint8_t                      is_ref;     /*!< Indicates this value is a reference */
	union {
		const pss_value_ref_t*   ref;        /*!< A refernece value */
		pss_bytecode_numeric_t   num;        /*!< A numeric value */
	};
} pss_value_const_t;
STATIC_ASSERTION_TYPE_COMPATIBLE(pss_value_t, is_ref, pss_value_const_t, is_ref);

/**
 * @brief Make a new runtime value 
 * @param type The type of the value
 * @param data The data of the value
 * @note Once we create a value and do not assigned this is marked as inactive and 0 refcnt, and
 *       The 0 refcnt will prevent the value from being disposed. However, onces incref is called
 *       the value will become active, and since then the reference counter will collect the value
 *       once it's not used anymore
 * @return The newly created value
 **/
pss_value_t pss_value_new(pss_value_ref_type_t type, void* data);

/**
 * @brief Increase the reference counter of the value
 * @param value The value 
 * @return status code 
 **/
int pss_value_incref(pss_value_t value);

/**
 * @brief Decrease the reference counter of the value
 * @param vvalue The value to oeprate
 * @return status code
 **/
int pss_value_decref(pss_value_t value);

/**
 * @brief Make a copy of the value
 * @param value The value to operate
 * @reteurn The newly created value
 **/
pss_value_t pss_value_copy(pss_value_const_t value);

/**
 * @brief Convert the value to string type and make a new value for it
 * @param value The value to convert
 * @note  If the 
 * @return The newly create value
 **/
pss_value_t pss_value_to_str(pss_value_const_t value);

/**
 * @brief Set the type specified operations for the given type
 * @param type The type code
 * @param ops  The operations
 * @return status code
 **/
int pss_value_set_type_ops(pss_value_ref_type_t type, pss_value_ref_ops_t ops);

/**
 * @brief Get the data pointer for this value
 * @param value The value 
 * @return The data pointer
 **/
void* pss_value_get_data(pss_value_const_t value);
#endif 
