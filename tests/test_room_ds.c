#include "room_ds.h"
#include "unity.h"

static struct room *room = NULL;

void
setUp(void) {
	room = room_alloc();
}

void
tearDown(void) {
	room_destroy(room);
	room = NULL;
}

void
test_lock(void) {
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	void **arr = NULL;

	LOCK_IF_GROW(arr, &mutex);
	TEST_ASSERT_EQUAL(4, arrcap(arr));

	/* Shouldn't grow. */
	LOCK_IF_GROW(arr, &mutex);
	TEST_ASSERT_EQUAL(4, arrcap(arr));

	arrsetlen(arr, 4);
	TEST_ASSERT_EQUAL(4, arrcap(arr));

	/* Should grow. */
	LOCK_IF_GROW(arr, &mutex);
	TEST_ASSERT_EQUAL(8, arrcap(arr));

	arrfree(arr);
}

void
test_insertion_deletion(void) {
	/* Empty timeline */
	TEST_ASSERT_EQUAL(-1, room_redact_event(room, 2500));

	/* 2499 - 1 backfill. */
	for (size_t i = 2499; i > 0; i--) {
		struct message *message
		  = message_alloc("Test", "@sender:localhost", 0, i, NULL, false);

		TEST_ASSERT_FALSE(message->reply);
		TEST_ASSERT_EQUAL(i, message->index);
		TEST_ASSERT_EQUAL(0,
		  room_put_message(room, &room->timelines[TIMELINE_BACKWARD], message));
	}

	/* 2500 - 4999 */
	for (size_t i = 2500; i < 5000; i++) {
		struct message *message
		  = message_alloc("Test", "@sender:localhost", 0, i, NULL, false);

		TEST_ASSERT_FALSE(message->reply);
		TEST_ASSERT_EQUAL(i, message->index);
		TEST_ASSERT_EQUAL(0,
		  room_put_message(room, &room->timelines[TIMELINE_FORWARD], message));
	}

	/* Array of random indices including min/max indices. */
	/* clang-format off */
	const size_t redact[] = {3380, 333, 1237, 944, 3365, 352, 3803, 1918, 1148, 1554, 196, 2630, 557, 4283, 262, 2371, 4754, 3233, 3993, 4637, 1693, 3452, 3869, 1911, 168, 1058, 4751, 83, 4063, 3350, 3260, 220, 3418, 4043, 4832, 675, 4587, 4238, 2462, 2807, 3177, 1073, 4368, 2059, 1944, 21, 3149, 3123, 2132, 2480, 611, 2396, 2812, 3366, 4883, 1040, 1211, 301, 4730, 3455, 4943, 2837, 4552, 1261, 962, 1281, 3421, 2922, 1580, 128, 2524, 638, 2712, 261, 1573, 4837, 2873, 4612, 524, 2129, 2574, 3354, 2920, 173, 1840, 3358, 2500, 3678, 3825, 2741, 3833, 1984, 3462, 3959, 1858, 4007, 1990, 105, 4600, 589, 4279, 2701, 4882, 4965, 1224, 4701, 376, 385, 3284, 396, 1225, 1541, 2294, 4205, 1, 4867, 3353, 1339, 800, 4800, 1698, 4997, 890, 3054, 1894, 976, 175, 3781, 1366, 3948, 2613, 3397, 2571, 4267, 4316, 3300, 100, 2993, 760, 4917, 4436, 3426, 4498, 1875, 3299, 1319, 2906, 2383, 2130, 2570, 4434, 87, 841, 956, 2136, 739, 3736, 4082, 1302, 3375, 52, 2955, 4510, 206, 4999, 4438, 2594, 499, 1718, 2243, 1303, 731, 531, 887, 975, 3800, 438, 1344, 2769, 1778, 805, 4881, 2453, 2969, 1473, 3842, 729, 1264, 3176, 2499, 1574, 2441, 1462, 3345, 3921, 4363, 474, 2038, 3030, 4297};
	/* clang-format on */

	const size_t invalid[] = {5000, 0, 5001};

	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		TEST_ASSERT_EQUAL(-1, room_redact_event(room, invalid[i]));
	}

	for (size_t i = 0; i < sizeof(redact) / sizeof(*redact); i++) {
		TEST_ASSERT_EQUAL(0, room_redact_event(room, redact[i]));
		struct room_index out;
		TEST_ASSERT_EQUAL(0, room_bsearch(room, redact[i], &out));
		TEST_ASSERT_EQUAL(
		  redact[i] < 2500 ? TIMELINE_BACKWARD : TIMELINE_FORWARD,
		  out.index_timeline);
		struct message *message
		  = room->timelines[out.index_timeline].buf[out.index_buf];
		TEST_ASSERT_NULL(message->body);
		TEST_ASSERT_TRUE(message->redacted);
	}

	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		TEST_ASSERT_EQUAL(-1, room_redact_event(room, invalid[i]));
	}
}

int
main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_lock);
	/* Tests bsearch aswell. */
	RUN_TEST(test_insertion_deletion);
	return UNITY_END();
}
