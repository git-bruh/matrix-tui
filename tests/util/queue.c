#include "util/queue.h"

#include "unity.h"

static struct queue queue = {0};
static int test_data[QUEUE_SIZE] = {0};

void
setUp(void) {
}

void
tearDown(void) {
	memset(&queue, 0, sizeof(queue));
}

static void
test_filling(void) {
	for (size_t i = 0; i < QUEUE_SIZE; i++) {
		TEST_ASSERT_EQUAL(0, queue_push_tail(&queue, &test_data[i]));
	}

	/* Push arbritary item after filling. */
	TEST_ASSERT_EQUAL(-1, queue_push_tail(&queue, &test_data[QUEUE_SIZE / 2]));
}

void
test_insertion(void) {
	test_filling();

	for (size_t i = 0; i < QUEUE_SIZE; i++) {
		int *tmp = queue_pop_head(&queue);
		TEST_ASSERT_NOT_NULL(tmp);
		TEST_ASSERT_EQUAL(&test_data[i], tmp);
	}

	TEST_ASSERT_NULL(queue_pop_head(&queue));
}

void
test_insertion_after_full(void) {
	test_filling();

	int *tmp = queue_pop_head(&queue);
	TEST_ASSERT_NOT_NULL(tmp);
	TEST_ASSERT_EQUAL(&test_data[0], tmp);

	int new_data = 1;

	TEST_ASSERT_EQUAL(0, queue_push_tail(&queue, &new_data));
	TEST_ASSERT_EQUAL(&new_data, queue.data[0]);
	TEST_ASSERT_EQUAL(&test_data[1], queue_pop_head(&queue));

	for (size_t i = 2; i < QUEUE_SIZE; i++) {
		TEST_ASSERT_EQUAL(&test_data[i], queue_pop_head(&queue));
	}

	/* Cycled back to the start. */
	TEST_ASSERT_EQUAL(&new_data, queue_pop_head(&queue));
	TEST_ASSERT_NULL(queue_pop_head(&queue));
}

int
main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_insertion);
	RUN_TEST(test_insertion_after_full);
	return UNITY_END();
}
