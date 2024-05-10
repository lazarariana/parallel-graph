// SPDX-License-Identifier: BSD-3-Clause

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "log/log.h"
#include "os_graph.h"
#include "os_threadpool.h"
#include "utils.h"

#define NUM_THREADS 4

static int sum;
static os_graph_t *graph;
static os_threadpool_t *tp;

pthread_mutex_t node_mutex = PTHREAD_MUTEX_INITIALIZER;

static void process_node(void *idx_ptr);

static void process_node(void *idx_ptr)
{
	os_node_t *node;
	unsigned int idx = *(unsigned int *)idx_ptr;

	pthread_mutex_lock(&node_mutex);

	node = graph->nodes[idx];

	if (graph->visited[idx] == DONE)
		goto process_node_done;

	if (graph->visited[idx] == PROCESSING) {
		sum += node->info;
		graph->visited[idx] = DONE;
	} else if (graph->visited[idx] == NOT_VISITED) {
		os_task_t *t =
			create_task((void *)process_node, (void *)&(node->id), NULL);

		// adaugam nou task in coada cu taskuri
		enqueue_task(tp, t);

		// consideram noul nod vizitat dar procesarea nu a avut loc inca
		graph->visited[idx] = PROCESSING;
	}

	for (unsigned int i = 0; i < node->num_neighbours; i++) {
		if (graph->visited[node->neighbours[i]] == NOT_VISITED) {
			// creem un nou task
			os_task_t *t = create_task((void *)process_node,
										(void *)&(node->neighbours[i]), NULL);

			// adaugam nou task in coada cu taskuri
			enqueue_task(tp, t);

			// consideram noul nod vizitat dar procesarea nu a avut loc inca
			graph->visited[node->neighbours[i]] = PROCESSING;
		}
	}
	// eliberam mutex-ul dupa ce am terminat de procesat vecinii

process_node_done:
	pthread_mutex_unlock(&node_mutex);
}

int main(int argc, char *argv[])
{
	FILE *input_file;
	int ret;
	unsigned int start;

	start = 0;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s input_file\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	input_file = fopen(argv[1], "r");
	DIE(input_file == NULL, "fopen");

	graph = create_graph_from_file(input_file);

	/* initialize a mutex */
	ret = pthread_mutex_init(&node_mutex, NULL);
	DIE(ret != 0, "pthread_mutex_init");

	tp = create_threadpool(NUM_THREADS);

	process_node((void *)&start);
	wait_for_completion(tp);

	// "distrugem mutex-ul" il deallocam
	pthread_mutex_destroy(&node_mutex);

	destroy_threadpool(tp);

	printf("%d", sum);

	return 0;
}
