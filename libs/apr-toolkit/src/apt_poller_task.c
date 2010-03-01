/*
 * Copyright 2008-2010 Arsen Chaloyan
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * $Id$
 */

#include "apt_poller_task.h"
#include "apt_task.h"
#include "apt_pool.h"
#include "apt_cyclic_queue.h"
#include "apt_log.h"


/** Poller task */
struct apt_poller_task_t {
	apr_pool_t         *pool;
	apt_task_t         *base;
	
	void               *obj;
	apt_poll_signal_f   signal_handler;
	apr_size_t          max_pollset_size;

	apr_thread_mutex_t *guard;
	apt_cyclic_queue_t *msg_queue;
	apt_pollset_t      *pollset;
	apt_timer_queue_t  *timer_queue;
};

static apt_bool_t apt_poller_task_msg_signal(apt_task_t *task, apt_task_msg_t *msg);
static apt_bool_t apt_poller_task_run(apt_task_t *task);
static apt_bool_t apt_poller_task_on_destroy(apt_task_t *task);


/** Create connection task */
APT_DECLARE(apt_poller_task_t*) apt_poller_task_create(
										apr_size_t max_pollset_size,
										apt_poll_signal_f signal_handler,
										void *obj,
										apt_task_msg_pool_t *msg_pool,
										apr_pool_t *pool)
{
	apt_task_vtable_t *vtable;
	apt_poller_task_t *task;

	if(!signal_handler) {
		return NULL;
	}
	
	task = apr_palloc(pool,sizeof(apt_poller_task_t));
	task->pool = pool;
	task->obj = obj;
	task->pollset = NULL;
	task->max_pollset_size = max_pollset_size;
	task->signal_handler = signal_handler;

	task->base = apt_task_create(task,msg_pool,pool);
	if(!task->base) {
		return NULL;
	}

	vtable = apt_task_vtable_get(task->base);
	if(vtable) {
		vtable->run = apt_poller_task_run;
		vtable->destroy = apt_poller_task_on_destroy;
		vtable->signal_msg = apt_poller_task_msg_signal;
	}
	apt_task_auto_ready_set(task->base,FALSE);

	task->msg_queue = apt_cyclic_queue_create(CYCLIC_QUEUE_DEFAULT_SIZE);
	apr_thread_mutex_create(&task->guard,APR_THREAD_MUTEX_UNNESTED,pool);

	task->timer_queue = apt_timer_queue_create(pool);
	return task;
}

/** Virtual destroy handler */
static apt_bool_t apt_poller_task_on_destroy(apt_task_t *base)
{
	apt_poller_task_t *task = apt_task_object_get(base);
	if(task->guard) {
		apr_thread_mutex_destroy(task->guard);
		task->guard = NULL;
	}
	if(task->msg_queue) {
		apt_cyclic_queue_destroy(task->msg_queue);
		task->msg_queue = NULL;
	}
	return TRUE;
}

/** Destroy connection task. */
APT_DECLARE(apt_bool_t) apt_poller_task_destroy(apt_poller_task_t *task)
{
	return apt_task_destroy(task->base);
}

/** Start poller task */
APT_DECLARE(apt_bool_t) apt_poller_task_start(apt_poller_task_t *task)
{
	return apt_task_start(task->base);
}

/** Terminate poller task */
APT_DECLARE(apt_bool_t) apt_poller_task_terminate(apt_poller_task_t *task)
{
	return apt_task_terminate(task->base,TRUE);
}

/** Get task */
APT_DECLARE(apt_task_t*) apt_poller_task_base_get(apt_poller_task_t *task)
{
	return task->base;
}

/** Get task vtable */
APT_DECLARE(apt_task_vtable_t*) apt_poller_task_vtable_get(apt_poller_task_t *task)
{
	return apt_task_vtable_get(task->base);
}

/** Get external object */
APT_DECLARE(void*) apt_poller_task_object_get(apt_poller_task_t *task)
{
	return task->obj;
}

/** Get pollset */
APT_DECLARE(apt_pollset_t*) apt_poller_task_pollset_get(apt_poller_task_t *task)
{
	return task->pollset;
}

/** Create timer */
APT_DECLARE(apt_timer_t*) apt_poller_task_timer_create(
									apt_poller_task_t *task, 
									apt_timer_proc_f proc, 
									void *obj, 
									apr_pool_t *pool)
{
	return apt_timer_create(task->timer_queue,proc,obj,pool);
}

/** Create the pollset */
static apt_bool_t apt_poller_task_pollset_create(apt_poller_task_t *task)
{
	task->pollset = apt_pollset_create((apr_uint32_t)task->max_pollset_size, task->pool);
	if(!task->pollset) {
		apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Create Pollset");
		return FALSE;
	}

	return TRUE;
}

/** Destroy the pollset */
static void apt_poller_task_pollset_destroy(apt_poller_task_t *task)
{
	if(task->pollset) {
		apt_pollset_destroy(task->pollset);
		task->pollset = NULL;
	}
}

static apt_bool_t apt_poller_task_wakeup_process(apt_poller_task_t *task)
{
	apt_bool_t status = TRUE;
	apt_bool_t running = TRUE;
	apt_task_msg_t *msg;

	do {
		apr_thread_mutex_lock(task->guard);
		msg = apt_cyclic_queue_pop(task->msg_queue);
		apr_thread_mutex_unlock(task->guard);
		if(msg) {
			status = apt_task_msg_process(task->base,msg);
		}
		else {
			running = FALSE;
		}
	}
	while(running == TRUE);
	return status;
}

static apt_bool_t apt_poller_task_run(apt_task_t *base)
{
	apt_poller_task_t *task = apt_task_object_get(base);
	apt_bool_t running = TRUE;
	apr_status_t status;
	apr_int32_t num;
	const apr_pollfd_t *ret_pfd;
	apr_interval_time_t timeout;
	apr_uint32_t queue_timeout;
	apr_time_t time_now, time_last = 0;
	int i;

	if(!task) {
		apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Start Network Client Task");
		return FALSE;
	}

	if(apt_poller_task_pollset_create(task) == FALSE) {
		apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Create Pollset");
		return FALSE;
	}

	/* explicitly indicate task is ready to process messages */
	apt_task_ready(task->base);

	while(running) {
		timeout = -1;
		if(apt_timer_queue_timeout_get(task->timer_queue,&queue_timeout) == TRUE) {
			timeout = queue_timeout * 1000;
			time_last = apr_time_now();
		}
		apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Wait for Task Messages [%s] timeout: %"APR_TIME_T_FMT,
			apt_task_name_get(task->base),
			timeout);
		status = apt_pollset_poll(task->pollset, timeout, &num, &ret_pfd);
		if(status != APR_SUCCESS && status != APR_TIMEUP) {
			apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Poll status: %d",status);
			continue;
		}
		for(i = 0; i < num; i++) {
			if(apt_pollset_is_wakeup(task->pollset,&ret_pfd[i])) {
				apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Process Control Message");
				if(apt_poller_task_wakeup_process(task) == FALSE) {
					running = FALSE;
					break;
				}
				continue;
			}

			apt_log(APT_LOG_MARK,APT_PRIO_DEBUG,"Process Message");
			task->signal_handler(task->obj,&ret_pfd[i]);
		}

		if(timeout != -1) {
			time_now = apr_time_now();
			if(time_now > time_last) {
				apt_timer_queue_advance(task->timer_queue,(apr_uint32_t)((time_now - time_last)/1000));
			}
		}
	}

	apt_poller_task_pollset_destroy(task);
	return TRUE;
}

static apt_bool_t apt_poller_task_msg_signal(apt_task_t *base, apt_task_msg_t *msg)
{
	apt_bool_t status;
	apt_poller_task_t *task = apt_task_object_get(base);
	apr_thread_mutex_lock(task->guard);
	status = apt_cyclic_queue_push(task->msg_queue,msg);
	apr_thread_mutex_unlock(task->guard);
	if(apt_pollset_wakeup(task->pollset) != TRUE) {
		apt_log(APT_LOG_MARK,APT_PRIO_WARNING,"Failed to Signal Control Message");
		status = FALSE;
	}
	return status;
}