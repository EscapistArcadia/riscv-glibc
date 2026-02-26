/* Copyright (C) 2002-2017 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include <errno.h>
#include <sched.h>
#include <string.h>
#include <sched.h>
#include "pthreadP.h"
#include <lowlevellock.h>
#include <virtuoso/pthread_types.h>
#include <virtuoso/pthread_utils.h>


int
pthread_setschedprio (pthread_t threadid, int prio)
{
  struct pthread *pd = (struct pthread *) threadid;

  if (pd->accel.id != -1) {
    if (prio < __sched_fifo_min_prio || prio > __sched_fifo_max_prio)
      return EINVAL;
    pd->accel.nprio = prio;

    if (pd->accel.is_active) {
      extern hpthread_intf_t intf;
      HIGH_DEBUG(printf("[HPTHREAD] Requested change of priority to %d for hpthread %s.\n", prio, pd->name);)
      // Check if the interface is IDLE. If yes, swap to BUSY. If not, block until it is
      while (!hpthread_intf_swap(VAM_IDLE, VAM_BUSY)) SCHED_YIELD;
      // Write the hpthread request to the interface
      intf.th = pd;
      // Set the interface state to SETPRIO
      hpthread_intf_set(VAM_SETPRIO);
      // Block until the request is complete (interface state is DONE), then swap to IDLE
      while (!hpthread_intf_swap(VAM_DONE, VAM_IDLE)) SCHED_YIELD;
      HIGH_DEBUG(printf("[HPTHREAD] Change of priority to %d complete for hpthread %s.\n", prio, pd->name);)
    }
    return 0;
  }

  /* Make sure the descriptor is valid.  */
  if (INVALID_TD_P (pd))
    /* Not a valid thread handle.  */
    return ESRCH;

  int result = 0;
  struct sched_param param;
  param.sched_priority = prio;

  /* See CREATE THREAD NOTES in nptl/pthread_create.c.  */
  lll_lock (pd->lock, LLL_PRIVATE);

  /* If the thread should have higher priority because of some
     PTHREAD_PRIO_PROTECT mutexes it holds, adjust the priority.  */
  if (__builtin_expect (pd->tpp != NULL, 0) && pd->tpp->priomax > prio)
    param.sched_priority = pd->tpp->priomax;

  /* Try to set the scheduler information.  */
  if (__glibc_unlikely (sched_setparam (pd->tid, &param) == -1))
    result = errno;
  else
    {
      /* We succeeded changing the kernel information.  Reflect this
	 change in the thread descriptor.  */
      param.sched_priority = prio;
      memcpy (&pd->schedparam, &param, sizeof (struct sched_param));
      pd->flags |= ATTR_FLAG_SCHED_SET;
    }

  lll_unlock (pd->lock, LLL_PRIVATE);

  return result;
}
