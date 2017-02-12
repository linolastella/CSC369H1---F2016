#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "traffic.h"

extern struct intersection isection;
extern struct car *in_cars[];
extern struct car *out_cars[];


/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with
 * its in_direction
 *
 */
void parse_schedule(char *file_name) {
	int id;
	struct car *cur_car;
	enum direction in_dir, out_dir;
	FILE *f = fopen(file_name, "r");

	/* parse file */
	while (fscanf(f, "%d %d %d", &id, (int*)&in_dir, (int*)&out_dir) == 3) {
		/* construct car */
		cur_car = malloc(sizeof(struct car));
		cur_car->id = id;
		cur_car->in_dir = in_dir;
		cur_car->out_dir = out_dir;

		/* append new car to head of corresponding list */
		cur_car->next = in_cars[in_dir];
		in_cars[in_dir] = cur_car;
		isection.lanes[in_dir].inc++;
	}

	fclose(f);
}

/**
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 *
 */
void init_intersection() {
	struct lane north_lane;
	struct lane south_lane;
	struct lane west_lane;
	struct lane east_lane;
	pthread_mutex_t lock1;
	pthread_mutex_t lock2;
	pthread_mutex_t lock3;
	pthread_mutex_t lock4;

	/* initialize locks */
	pthread_mutex_init(&lock1, NULL);
	pthread_mutex_init(&lock2, NULL);
	pthread_mutex_init(&lock3, NULL);
	pthread_mutex_init(&lock4, NULL);

	isection.quad[0] = lock1;
	isection.quad[1] = lock2;
	isection.quad[2] = lock3;
	isection.quad[3] = lock4;

	/* initialize lanes */
	init_lane(&north_lane);
	init_lane(&south_lane);
	init_lane(&west_lane);
	init_lane(&east_lane);

	isection.lanes[0] = north_lane;
	isection.lanes[1] = south_lane;
	isection.lanes[2] = east_lane;
	isection.lanes[3] = west_lane;
}

void init_lane(struct lane *l) {
	pthread_mutex_t lock;
	pthread_cond_t  producer_cv;
	pthread_cond_t consumer_cv;

	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&producer_cv, NULL);
	pthread_cond_init(&consumer_cv, NULL);

	/* construct a lane */
	l->lock = lock;
	l->producer_cv = producer_cv;
	l->consumer_cv = consumer_cv;
	l->inc = 0;
	l->passed = 0;
	l->buffer = malloc(sizeof(struct car*) * LANE_LENGTH);
	l->head = LANE_LENGTH - 1;
	l->tail = LANE_LENGTH - 1;
	l->capacity = LANE_LENGTH;
	l->in_buf = 0;

}

/**
 *
 * Populates the corresponding lane with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lane.
 *
 */
void *car_arrive(void *arg) {
	struct lane *l = &isection.lanes[*(int*) arg];
	struct car *cur_car;

	cur_car = in_cars[*(int*) arg];
	while (cur_car != NULL) {
		pthread_mutex_lock(&l->lock);

		/* check if there is space available */
		if (l->in_buf == l->capacity) {
			pthread_cond_wait(&l->producer_cv, &l->lock);
		}

		/* add to the buffer */
	    l->buffer[l->tail] = cur_car;

	    /* update tail and in_buf */
	    l->in_buf++;
	    l->tail = (l->tail != 0) ? l->tail - 1 : l->capacity - 1;
	    
		cur_car = cur_car->next;

		pthread_cond_signal(&l->consumer_cv);
		pthread_mutex_unlock(&l->lock);
	}

	return NULL;
}

/**
 *
 * Moves cars from a single lane across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lane.
 */
void *car_cross(void *arg) {
	struct lane *l = &isection.lanes[*(int*) arg];
	struct car *cur_car;
	int *path;
	int i;

	pthread_mutex_lock(&l->lock);

	/* check if there is any car ready to cross */
	if (l->in_buf == 0) {
		pthread_cond_wait(&l->consumer_cv, &l->lock);
	}

	cur_car = l->buffer[l->head];
	while (l->inc != l->passed) {

		if (cur_car != NULL) {
			path = compute_path(cur_car->in_dir, cur_car->out_dir);

			/* acquiring locks */
			for (i = 0; i <= 3; i++) {
				if (path[i] != 0) {
					/* path[i] - 1 is the correct lock for a quadrant */
					pthread_mutex_lock(&isection.quad[path[i] - 1]);
				}
			}

			/* add car to out_cars and remove it from the buffer */
			cur_car->next = out_cars[*(int*) arg];
			out_cars[*(int*) arg] = cur_car;
			l->buffer[l->head] = NULL;

			/* update head, passed and in_buf */
			l->head = (l->head != 0) ? l->head - 1 : l->capacity - 1;
			l->in_buf--;
			l->passed++;

			/* free the path and unlock appropriate locks */
			for (i = 0; i < 3; i++) {
				if (path[i] != 0) {
					pthread_mutex_unlock(&isection.quad[path[i] - 1]);
				}
			}
			free(path);

			fprintf(stdout, "Car crossed: id: %d, in_dir: %d, out_dir: %d\n",
				cur_car->id, cur_car->in_dir, cur_car->out_dir);
				
			cur_car = l->buffer[l->head];
			l->inc--;

			pthread_cond_signal(&l->producer_cv);
		}
	}
	pthread_mutex_unlock(&l->lock);

	return NULL;
}

/**
 *
 * Given a car's in_dir and out_dir return a sorted
 * list of the quadrants the car will pass through.
 *
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {
	int *lst = (int *)calloc(3, sizeof(int));

	if (in_dir == NORTH) {
		if (out_dir == NORTH) {
			lst[0] = 1;
			lst[1] = 2;
		} else if (out_dir == SOUTH) {
			lst[0] = 2;
			lst[1] = 3;
		} else if (out_dir == WEST) {
			lst[0] = 2;
		} else if (out_dir == EAST) {
			lst[0] = 2;
			lst[1] = 3;
			lst[2] = 4;
		} else {
			fprintf(stderr, "Invalid out_dir.\n");
			exit(1);
		}

	} else if (in_dir == SOUTH) {
		if (out_dir == NORTH) {
			lst[0] = 1;
			lst[1] = 4;
		} else if (out_dir == SOUTH) {
			lst[0] = 3;
			lst[1] = 4;
		} else if (out_dir == WEST) {
			lst[0] = 1;
			lst[1] = 2;
			lst[2] = 4;
		} else if (out_dir == EAST) {
			lst[0] = 4;
		} else {
			fprintf(stderr, "Invalid out_dir.\n");
			exit(1);
		}

	} else if (in_dir == WEST) {
		if (out_dir == NORTH) {
			lst[0] = 1;
			lst[1] = 3;
			lst[2] = 4;
		} else if (out_dir == SOUTH) {
			lst[0] = 3;
		} else if (out_dir == WEST) {
			lst[0] = 2;
			lst[1] = 3;
		} else if (out_dir == EAST) {
			lst[0] = 3;
			lst[1] = 4;
		} else {
			fprintf(stderr, "Invalid out_dir.\n");
			exit(1);
		}

	} else if (in_dir == EAST) {
		if (out_dir == NORTH) {
			lst[0] = 1;
		} else if (out_dir == SOUTH) {
			lst[0] = 1;
			lst[1] = 2;
			lst[2] = 3;
		} else if (out_dir == WEST) {
			lst[0] = 1;
			lst[1] = 2;
		} else if (out_dir == EAST) {
			lst[0] = 1;
			lst[1] = 4;
		} else {
			fprintf(stderr, "Invalid out_dir.\n");
			exit(1);
		}

	} else {
		fprintf(stderr, "Invalid in_dir.\n");
		exit(1);
	}

	return lst;
}

