/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The protocol type database system
 * @details This is the system that actually manages all the type registered in the
 *         system. It will use a cache for all the loaded types
 * @file proto/include/proto/db.h
 **/

#ifndef __PROTO_DB_H_
#define __PROTO_DB_H_

/**
 * @brief The property mask
 **/
typedef enum {
	PROTO_DB_FIELD_PROP_ERROR   = -1,         /*!< The error status */
	PROTO_DB_FIELD_PROP_NUMERIC = 1,          /*!< If this is a numeric value */
	PROTO_DB_FIELD_PROP_SIGNED  = 2,          /*1< If this is a signed value */
	PROTO_DB_FIELD_PROP_REAL    = 4,          /*!< If this is a real number */
	PROTO_DB_FIELD_PROP_SCOPE   = 8,          /*!< If this is a scope */
	PROTO_DB_FIELD_PROP_PRIMITIVE_SCOPE = 16  /*!< If this is a primitive scope */
} proto_db_field_prop_t;


/**
 * @brief The information about a field
 **/
typedef struct {
	const char*           name;              /*!< The name of the field */
	const char*           type;              /*!< The type name of the field */
	uint32_t              size;              /*!< The size of the field */
	uint32_t              offset;            /*!< The offset of the field, frome the begining of the type */
	proto_db_field_prop_t primitive_prop;    /*!< The primitive props */
	uint32_t              ndims;             /*!< The number of dimensions */
	const uint32_t*       dims;              /*!< The dimension size */
	uint32_t              is_alias:1;        /*!< If this field is a name alias */
} proto_db_field_info_t;

/**
 * @brief The callback function type used to traverse the all the field in the given type
 * @param info The information about current field
 * @param data The additional data would pass to the callback
 * @return status code
 **/
typedef int (*proto_db_field_callback_t)(proto_db_field_info_t info, void* data);

/**
 * @brief initialize the protocol database
 * @note for each servlet uses the protocol database, this function should be called in the init callback and
 *       the finailize function should be called in the cleanup callback to avoid memory leak.
 * @return status code
 **/
int proto_db_init(void);

/**
 * @brief dispose a used protocol database context
 * @return status code
 **/
int proto_db_finalize(void);

/**
 * @brief query a type from given type name
 * @param type_name the name of the type to query
 * @return the protocol type object for this type
 **/
const proto_type_t* proto_db_query_type(const char* type_name);

/**
 * @brief compute the actual size of the protocol type
 * @param type_name the protocol type object to compute
 * @note This function doesn't support the adhoc primitive type
 * @return the size of the type or error code
 **/
uint32_t proto_db_type_size(const char* type_name);

/**
 * @brief compute the offset from the beging of the type to the queried field.
 * @note  The name is name of the field. <br/>
 *        For example, for a Point3D, we can query <br/>
 *        proto_db_type_offset(point3d_type, "y") and it will return 4 <br/>
 *        This function also accept the complex name, for example in a triangle3D type,
 *        we can use proto_db_type_offset(triangle3d_type, "vertices[0].x" for the x coordinate
 *        of the first vertex.
 * @param type_name the name of the type
 * @param fieldname the field name descriptor
 * @param size_buf the buffer used to return the data size of this name expression, NULL if we don't want the info
 * @return the offset from the beginging of the data strcturue, or error code
 **/
uint32_t proto_db_type_offset(const char* type_name, const char* fieldname, uint32_t* size_buf);

/**
 * @brief Get the field name of the type
 * @note If the field is a primitive, it will returns an adhoc data type for the primitves, for example uint32, etc
 *       For this kinds of virtual typename, the only thing we can access is &lt;typename&gt;.value, which has the primitive
 *       type
 * @param type_name The type name
 * @param fieldname The field name descriptor
 * @return The field type name
 **/
const char* proto_db_field_type(const char* type_name, const char* fieldname);

/**
 * @brief validate the type is well formed, which means the type's references are all defined.
 *        And there's no cirular dependcy with the type, all the alias are valid
 * @param type_name the name of the type to validate
 * @return status code
 **/
int proto_db_type_validate(const char* type_name);

/**
 * @brief get the common ancestor for the list of type names
 * @param type_name the list of type names we want to compute, it should ends with NULL pointer
 * @return the result type name or NULL on error case
 * @note if the buffer don't have enough space for the result, we will return an error code
 **/
const char* proto_db_common_ancestor(char const* const* type_name);

/**
 * @brief Get the type name string that managed by libproto
 * @param name The name value
 * @return The pointer
 **/
const char* proto_db_get_managed_name(const char* name);

/**
 * @brief Check if this field is numeric value
 * @param  type_name The type name we want to inspect
 * @param  field The field name
 * @return check result or error code
 **/
proto_db_field_prop_t proto_db_field_type_info(const char* type_name, const char* field);

/**
 * @brief Get the scope type id that the scope token stands for
 * @param type_name The type name we want to check
 * @param field The field
 * @return The type id
 **/
const char* proto_db_field_scope_id(const char* type_name, const char* field);

/**
 * @brief Get the default value of a field
 * @note  The field must be a numeric field
 * @param type_name The type name
 * @param field The field name
 * @param buf The data buffer
 * @param sizebuf The size buffer
 * @return The number of default value has been get from the field, or error code
 **/
int proto_db_field_get_default(const char* type_name, const char* field, const void** buf, size_t* sizebuf);

/**
 * @brief traverse all the field of the given type name
 * @param type_name The name of the type
 * @param func The traverse function
 * @param data The addtional data that would be passed
 * @return status ccode
 **/
int proto_db_type_traverse(const char* type_name, proto_db_field_callback_t func, void* data);

/**
 * @brief Check if the type is an adhoc type
 * @param type_name The name of the type
 * @param info_buf The information buffer, if non-NULL is passed, the type information will be written
 * @return the check result or error code
 **/
int proto_db_is_adhoc(const char* type_name, proto_db_field_info_t* info_buf);

#endif /* __PROTO_DB_H_ */
