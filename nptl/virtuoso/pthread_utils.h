#ifndef __VIRTUOSO_ACCEL_TYPES_H__
#define __VIRTUOSO_ACCEL_TYPES_H__

#include <virtuoso/pthread_types.h>

/**
 * @brief Traverses the /dev file systems to find available accelerators and populate the global list of accelerators.
 *        This function is called during the initialization of the pthread library now.
 *
 * @todo Do we need to initialize the VAM here?
 * @todo How can we check if the user app wants to use accelerators? Do we need to do this at all?
 * @todo Still hacky, permissions? security? multiprocessing?
 * 
 * @return int 
 */
extern int __pthread_probe_accelerators(void) internal_function attribute_hidden;

#endif
