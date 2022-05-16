#include "ui/tab_room.h"

#include "app/state.h"
#include "unity.h"

enum rooms_tag {
	R1 = 0,
	R2,
	R3,
	R4,
	R_MAX,
	R_TERM = -1,
};

typedef enum rooms_tag room_children_t[R_MAX];
typedef enum rooms_tag expected_nodes_t[NODE_MAX];

static struct state_rooms state_rooms;
static struct tab_room tab_room;

static char R_TO_STR[R_MAX][3]
  = {[R1] = {"R1"}, [R2] = {"R2"}, [R3] = {"R3"}, [R4] = {"R4"}};

static void
test_finish_state_rooms(void);

void
setUp(void) {
	TEST_ASSERT_EQUAL(0, tab_room_init(&tab_room));
}

void
tearDown(void) {
	tab_room_finish(&tab_room);
	test_finish_state_rooms();
}

static void
test_init_state_rooms(const room_children_t children[]) {
	for (size_t i = 0; i < R_MAX; i++) {
		struct room *room = room_alloc((struct room_info) {0});
		shput(state_rooms.rooms, R_TO_STR[i], room);
	}

	for (size_t i = 0; i < R_MAX; i++) {
		for (size_t j = 0; children[i][j] != R_TERM; j++) {
			state_rooms.rooms[i].value->info.is_space = true;
			room_add_child(
			  state_rooms.rooms[i].value, R_TO_STR[children[i][j]]);
		}
	}

	state_reset_orphans(&state_rooms);

	for (size_t i = 0; i < shlenu(state_rooms.orphaned_rooms); i++) {
		struct room *room
		  = shget(state_rooms.rooms, state_rooms.orphaned_rooms[i].key);
		TEST_ASSERT_NOT_NULL(room);
	}
}

static void
test_finish_state_rooms(void) {
	for (size_t i = 0; i < shlenu(state_rooms.rooms); i++) {
		room_destroy(state_rooms.rooms[i].value);
	}

	shfree(state_rooms.rooms);
	shfree(state_rooms.orphaned_rooms);

	memset(&state_rooms, 0, sizeof(state_rooms));
}

static void
assert_expected_nodes(const expected_nodes_t nodes[]) {
	for (enum tab_room_nodes node = 0; node < NODE_MAX; node++) {
		size_t i = 0;

		for (; nodes[node][i] != R_TERM; i++) {
			TEST_ASSERT_LESS_THAN(arrlenu(tab_room.root_nodes[node].nodes), i);
			TEST_ASSERT_EQUAL(tab_room.root_nodes[node].nodes[i]->data,
			  &state_rooms.rooms[nodes[node][i]]);
		}

		TEST_ASSERT_EQUAL(i, arrlenu(tab_room.root_nodes[node].nodes));
	}
}

void
test_basic(void) {
	const room_children_t children[] = {
	  [R1] = {	  R2, R4, R_TERM},
	  [R2] = {R_TERM	},
	  [R3] = {R_TERM	},
	  [R4] = {R_TERM	},
	};

	test_init_state_rooms(children);

	tab_room_reset_rooms(&tab_room, &state_rooms);

	TEST_ASSERT_EQUAL(
	  tab_room.root_nodes[NODE_SPACES].nodes[0]->data, tab_room.selected_room);
	TEST_ASSERT_EQUAL(&state_rooms.rooms[R1], tab_room.selected_room);

	{
		const expected_nodes_t nodes[] = {
		  [NODE_INVITES] = {R_TERM},
		  [NODE_SPACES] = { R1,R_TERM},
		  [NODE_DMS] = { R_TERM	  },
		  [NODE_ROOMS] = { R3,	  R_TERM},
		};

		assert_expected_nodes(nodes);
	}

	arrput(tab_room.path, R_TO_STR[R1]);

	tab_room_reset_rooms(&tab_room, &state_rooms);

	{
		const expected_nodes_t nodes[] = {
		  [NODE_INVITES] = {R_TERM },
		  [NODE_SPACES] = { R_TERM},
		  [NODE_DMS] = { R_TERM	  },
		  [NODE_ROOMS] = { R2,		  R4, R_TERM},
		};

		assert_expected_nodes(nodes);
	}
}

void
test_recursive(void) {
}

int
main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_basic);
	RUN_TEST(test_recursive);
	return UNITY_END();
}
