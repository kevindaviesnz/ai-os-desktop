#ifndef OS_AUTARKY_H
#define OS_AUTARKY_H

#include "os_types.h"

/* QA FIX: Explicit Gas Limit to prevent EL1 kernel lockup */
#define ATK_GAS_LIMIT 10000

/* Initializes the Autarky runtime environment */
void autarky_init(void);

/* Executes an Autarky bytecode buffer.
 * Enforces linear types and O(1) memory traversal.
 * Returns the total gas consumed.
 */
uint32_t autarky_execute(const char *bytecode, char *out_buf, uint32_t max_len);

#endif