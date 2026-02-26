#include <assert.h>
#include <pthreadP.h>
#include <virtuoso/esp/libesp.h>

int __pthread_attr_setmmiobase_np(pthread_attr_t *attr, void *mem, void *buf2handle_list) {
    assert(sizeof (*attr) >= sizeof (struct pthread_attr));

    struct pthread_attr *iattr = (struct pthread_attr *) attr;

    iattr->accel_attr.mem = mem;
    extern buf2handle_node *head;
    head = buf2handle_list;
    iattr->flags |= ATTR_FLAG_ACCELERATOR;
    return 0;
}
strong_alias(__pthread_attr_setmmiobase_np, pthread_attr_setmmiobase_np)
