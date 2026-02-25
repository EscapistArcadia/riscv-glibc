#ifndef __VIRTUOSO_PTHREAD_UTILS_H__
#define __VIRTUOSO_PTHREAD_UTILS_H__

#include <virtuoso/pthread_types.h>
#include <stdint.h>
#include <stdbool.h>

// State enumeration for hpthread request interface
#define VAM_RESET 0
#define VAM_WAKEUP 1
#define VAM_IDLE 2
#define VAM_BUSY 3
#define VAM_DONE 4
#define VAM_CREATE 5
#define VAM_JOIN 6
#define VAM_SETPRIO 7
#define VAM_REPORT 8
#define VAM_QUERY 9

// hpthread interface definition
typedef struct {
    volatile uint8_t state; // Interface synchronization variable
    struct pthread *th; // hpthread for the request
    hpthread_cand_t *list; // hpthread candidate list
} hpthread_intf_t;

// Helper function for swapping the state of the interface
bool hpthread_intf_swap(uint8_t expected_value, uint8_t new_value);
// Helper function for testing the state of the interface
uint8_t hpthread_intf_test(void);
// Helper function for setting the state of the interface
void hpthread_intf_set(uint8_t set_value);

// /**
//  * @brief Traverses the /dev file systems to find available accelerators and populate the global list of accelerators.
//  *        This function is called during the initialization of the pthread library now.
//  *
//  * @todo Do we need to initialize the VAM here?
//  * @todo How can we check if the user app wants to use accelerators? Do we need to do this at all?
//  * @todo Still hacky, permissions? security? multiprocessing?
//  * 
//  * @return int 
//  */
// extern int __pthread_probe_accelerators(void) internal_function attribute_hidden;

#endif
