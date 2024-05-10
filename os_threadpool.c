// SPDX-License-Identifier: BSD-3-Clause
#include "os_threadpool.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "log/log.h"
#include "utils.h"

/* Create a task that would be executed by a thread. */
os_task_t *create_task(void (*action)(void *), void *arg,
					   void (*destroy_arg)(void *))
{
	os_task_t *t;

	t = malloc(sizeof(*t));
	DIE(t == NULL, "malloc");

	t->action = action;            // the function
	t->argument = arg;             // arguments for the function
	t->destroy_arg = destroy_arg;  // destroy argument function

	return t;
}

/* Destroy task. */
void destroy_task(os_task_t *t)
{
	if (t->destroy_arg != NULL)
		t->destroy_arg(t->argument);

	free(t);
}

/* Put a new task to threadpool task queue. */
void enqueue_task(os_threadpool_t *tp, os_task_t *t)
{
	assert(tp != NULL);
	assert(t != NULL);

	pthread_mutex_lock(&tp->list_mutex);

	list_add_tail(&tp->head, &t->list);

	pthread_cond_signal(&tp->task_added);

	pthread_mutex_unlock(&tp->list_mutex);
}

/*
 * Check if queue is empty.
 * This function should be called in a synchronized manner.
 */
static int queue_is_empty(os_threadpool_t *tp)
{
	return list_empty(&tp->head);
}

/*
 * Get a task from threadpool task queue.
 * Block if no task is available.
 * Return NULL if work is complete, i.e. no task will become available,
 * i.e. all threads are going to block.
 */

os_task_t *dequeue_task(os_threadpool_t *tp)
{
	os_task_t *t;

	pthread_mutex_lock(&tp->list_mutex);

	while (queue_is_empty(tp) && (tp->work_not_done)) {
		// avem un nou thread care asteapta task-uri
		tp->task_waiting++;
		pthread_cond_signal(&tp->thread_onhold);
		pthread_cond_wait(&tp->task_added, &tp->list_mutex);

		// avem un nou thread care incepe lucru la task (poate adauga noi taskuri in lista)
		tp->task_waiting--;
	}

	// suntem inca in mutex lock;

	// lista nu trebuie sa fie goala ca sa poata extrage din ea
	if (!queue_is_empty(tp)) {
		os_list_node_t *tmp_node = tp->head.next;

		t = list_entry(tmp_node, os_task_t, list);

		// stergem nodul curent
		list_del(tmp_node);
		pthread_mutex_unlock(&tp->list_mutex);

		// returnam taskul curent
		return t;
	}
	// daca nu mai sunt taskuri iesim cu NULL ca sa terminam thread-ul
	pthread_mutex_unlock(&tp->list_mutex);

	return NULL;
}

/* Loop function for threads */
static void *thread_loop_function(void *arg)
{
	os_threadpool_t *tp = (os_threadpool_t *)arg;

	while (1) {
		os_task_t *t;

		t = dequeue_task(tp);
		if (t == NULL)
			break;

		t->action(t->argument);

		destroy_task(t);
	}

	return NULL;
}

/* Wait completion of all threads. This is to be called by the main thread. */
void wait_for_completion(os_threadpool_t *tp)
{
	pthread_mutex_lock(&tp->list_mutex);
  // facem bucla pana cand toate thredurile sunt in asteptare
	while (tp->task_waiting < tp->num_threads)
		// asteptam semnalul ca un nou thread a intrat in asteptare
		pthread_cond_wait(&tp->thread_onhold, &tp->list_mutex);

	tp->work_not_done = 0;
	pthread_cond_broadcast(&tp->task_added);

	// signal to exit the thread_loop
	pthread_mutex_unlock(&tp->list_mutex);

	/* Join all worker threads. */
	for (unsigned int i = 0; i < tp->num_threads; i++)
		pthread_join(tp->threads[i], NULL);
}

/* Create a new threadpool. */
os_threadpool_t *create_threadpool(unsigned int num_threads)
{
	os_threadpool_t *tp = NULL;
	int rc;
	int ret;

	tp = calloc(1, sizeof(*tp));
	DIE(tp == NULL, "malloc");

	list_init(&tp->head);

	// initializam variabilele
	tp->num_threads = num_threads;
	tp->work_not_done = 1;
	tp->task_waiting = 0;

	ret = pthread_mutex_init(&tp->list_mutex, NULL);
	DIE(ret != 0, "pthread_mutex_init");

	ret = pthread_cond_init(&tp->thread_onhold, NULL);
	DIE(ret != 0, "pthread_cond_init");

	ret = pthread_cond_init(&tp->task_added, NULL);
	DIE(ret != 0, "pthread_cond_init");

	tp->threads = malloc(num_threads * sizeof(*tp->threads));

	DIE(tp->threads == NULL, "malloc");
	for (unsigned int i = 0; i < num_threads; ++i) {
		rc = pthread_create(&tp->threads[i], NULL, &thread_loop_function,
							(void *)tp);
		DIE(rc < 0, "pthread_create");
	}

	return tp;
}

/* Destroy a threadpool. Assume all threads have been joined. */
void destroy_threadpool(os_threadpool_t *tp)
{
	os_list_node_t *n, *p;

	pthread_mutex_destroy(&tp->list_mutex);
	pthread_cond_destroy(&tp->thread_onhold);
	pthread_cond_destroy(&tp->task_added);

	list_for_each_safe(n, p, &tp->head) {
		list_del(n);
		destroy_task(list_entry(n, os_task_t, list));
	}

	free(tp->threads);
	free(tp);
}
