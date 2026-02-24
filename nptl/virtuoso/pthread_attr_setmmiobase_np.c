#include <assert.h>
#include <pthreadP.h>

int __pthread_attr_setmmiobase_np(pthread_attr_t *attr, void *mem) {
    assert(sizeof (*attr) >= sizeof (struct pthread_attr));

    struct pthread_attr *iattr = (struct pthread_attr *) attr;

    iattr->accel_attr.mem = mem;
    iattr->flags |= ATTR_FLAG_ACCELERATOR;
    return 0;
}
strong_alias(__pthread_attr_setmmiobase_np, pthread_attr_setmmiobase_np)
