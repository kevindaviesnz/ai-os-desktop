#ifndef OS_TYPES_H
#define OS_TYPES_H

/* Bare-metal freestanding types */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

/* Compile-time assertion for bare-metal bounds checking */
#ifndef STATIC_ASSERT
#define STATIC_ASSERT(cond, msg) typedef char static_assertion_##msg[(cond) ? 1 : -1]
#endif

#endif