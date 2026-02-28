#include <assert.h>
#include <stdint.h>
#include <pthreadP.h>

int __pthread_attr_setsmqueue_np(pthread_attr_t *attr, unsigned queue_ptr) {
    assert(sizeof (*attr) >= sizeof (struct pthread_attr));

    struct pthread_attr *iattr = (struct pthread_attr *) attr;

    iattr->accel_attr.queue_ptr = queue_ptr;
    iattr->flags |= ATTR_FLAG_ACCELERATOR;
    return 0;
}
strong_alias(__pthread_attr_setsmqueue_np, pthread_attr_setsmqueue_np)
