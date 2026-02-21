#include <assert.h>
#include "pthreadP.h"
#include <printf.h>

int __pthread_attr_setprimitive_np(pthread_attr_t *attr, accel_primitive_t prim) {
    assert(sizeof (*attr) >= sizeof (struct pthread_attr));

    struct pthread_attr *iattr = (struct pthread_attr *) attr;
    if (!primitive_is_valid(prim)) {
        return EINVAL;
    }
    
    // printf("Setting primitive to %d\n", prim);
    iattr->accel.prim = prim;
    iattr->flags |= ATTR_FLAG_ACCELERATOR;
    return 0;
}
strong_alias(__pthread_attr_setprimitive_np, pthread_attr_setprimitive_np)
