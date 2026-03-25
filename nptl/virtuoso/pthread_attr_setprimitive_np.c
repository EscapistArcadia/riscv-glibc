#include <assert.h>
#include <stdio.h>
#include <pthreadP.h>

int __pthread_attr_setprimitive_np(pthread_attr_t *attr, accel_prim_t prim, int cpu_invoke) {
    assert(sizeof (*attr) >= sizeof (struct pthread_attr));

    struct pthread_attr *iattr = (struct pthread_attr *) attr;
    if (!primitive_is_valid(prim)) {
        return EINVAL;
    }

    // printf("Setting primitive to %d\n", prim);
    iattr->accel_attr.prim = prim;
    iattr->flags |= ATTR_FLAG_ACCELERATOR;
    iattr->accel_attr.cpu_invoke = cpu_invoke;
    return 0;
}
strong_alias(__pthread_attr_setprimitive_np, pthread_attr_setprimitive_np)
