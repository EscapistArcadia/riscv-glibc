#ifndef __VAM_INTERFACES_H
#define __VAM_INTERFACES_H

#include <stdint.h>
#include <stdbool.h>

int wakeup_vam(void);

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
    struct hpthread_cand_t *list; // hpthread candidate list
} hpthread_intf_t;

// Helper function for swapping the state of the interface
bool hpthread_intf_swap(uint8_t expected_value, uint8_t new_value);
// Helper function for testing the state of the interface
uint8_t hpthread_intf_test(void);
// Helper function for setting the state of the interface
void hpthread_intf_set(uint8_t set_value);


#endif
