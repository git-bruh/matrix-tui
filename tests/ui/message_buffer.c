#include "ui/message_buffer.h"

#include "app/room_ds.h"
#include "unity.h"

struct message_buffer buf = {0};

/* Start at x = 20, y = 2, end = 11, height 9 */
struct widget_points points = {.x1 = 20, .x2 = 80, .y1 = 2, .y2 = 10 + 1};

struct message messages[20] = {0};

static char sender[] = "@Hello:localhost";
static uint32_t *body;
static struct members_map *map = NULL;
static uint32_t *name = NULL;
static uint32_t **names = NULL;

void
setUp(void) {
	body = buf_to_uint32_t("Hello", 0);
	message_buffer_init(&buf);

	for (size_t i = 0; i < (sizeof(messages) / sizeof(*messages)); i++) {
		messages[i].index = i;
		messages[i].body = body;
		messages[i].sender = sender;
		messages[i].username = body;
	}

	SHMAP_INIT(map);
	name = buf_to_uint32_t("Hello", 0);
	arrput(names, name);
	shput(map, sender, names);
}

void
tearDown(void) {
	arrfree(body);
	message_buffer_finish(&buf);
	memset(messages, 0, sizeof(*messages));
	shfree(map);
	arrfree(name);
	arrfree(names);
	map = NULL;
	name = NULL;
	names = NULL;
}

static void
check_select(struct message *last, struct message *prev_to_last) {
	const int correct_xy[][2] = {
	  {20, 10},
	  {25, 10},
	};

	const int bad_xy[][2] = {
	  {19, 10},
	  { 5, 11},
	  {25,  1},
	};

	for (size_t i = 0; i < sizeof(correct_xy) / sizeof(*correct_xy); i++) {
		buf.selected = NULL;

		/* Select current message. */
		TEST_ASSERT_EQUAL(WIDGET_REDRAW,
		  message_buffer_handle_event(
			&buf, MESSAGE_BUFFER_SELECT, correct_xy[i][0], correct_xy[i][1]));
		TEST_ASSERT_EQUAL(last, buf.selected);
		/* Select current message again to unselect it. */
		TEST_ASSERT_EQUAL(WIDGET_REDRAW,
		  message_buffer_handle_event(
			&buf, MESSAGE_BUFFER_SELECT, correct_xy[i][0], correct_xy[i][1]));
		TEST_ASSERT_EQUAL(NULL, buf.selected);

		/* Do the same, but for the previous message. */
		if (prev_to_last) {
			TEST_ASSERT_EQUAL(WIDGET_REDRAW,
			  message_buffer_handle_event(&buf, MESSAGE_BUFFER_SELECT,
				correct_xy[i][0], correct_xy[i][1] - 1));
			TEST_ASSERT_EQUAL(prev_to_last, buf.selected);
			TEST_ASSERT_EQUAL(WIDGET_REDRAW,
			  message_buffer_handle_event(&buf, MESSAGE_BUFFER_SELECT,
				correct_xy[i][0], correct_xy[i][1] - 1));
			TEST_ASSERT_EQUAL(NULL, buf.selected);
		}

		/* Select the current message again. */
		TEST_ASSERT_EQUAL(WIDGET_REDRAW,
		  message_buffer_handle_event(
			&buf, MESSAGE_BUFFER_SELECT, correct_xy[i][0], correct_xy[i][1]));
		TEST_ASSERT_EQUAL(last, buf.selected);

		/* Ensure that out of bounds clicks don't affect the selected message.
		 */
		for (size_t j = 0; j < sizeof(bad_xy) / sizeof(*bad_xy); j++) {
			TEST_ASSERT_EQUAL(WIDGET_NOOP,
			  message_buffer_handle_event(
				&buf, MESSAGE_BUFFER_SELECT, bad_xy[j][0], bad_xy[j][1]));
			TEST_ASSERT_EQUAL(last, buf.selected);
		}
	}
}

static void
action() {
	size_t size = sizeof(messages) / sizeof(*messages);

	TEST_ASSERT_EQUAL(-1, message_buffer_redact(&buf, 2));

	for (size_t i = 0; i < size; i++) {
		TEST_ASSERT_EQUAL(
		  0, message_buffer_insert(&buf, &points, &messages[i]));
	}

	/* Select the topmost message and ensure the correct index. */
	TEST_ASSERT_EQUAL(WIDGET_REDRAW,
	  message_buffer_handle_event(&buf, MESSAGE_BUFFER_SELECT, 20, 2));
	TEST_ASSERT_EQUAL(&messages[size - 9], buf.selected);
	/* Unselect. */
	TEST_ASSERT_EQUAL(WIDGET_REDRAW,
	  message_buffer_handle_event(&buf, MESSAGE_BUFFER_SELECT, 20, 2));
	TEST_ASSERT_EQUAL(NULL, buf.selected);

	for (size_t i = size; i > 0; i--) {
		if ((i - 1) > 0) {
			check_select(&messages[i - 1], &messages[i - 2]);
			TEST_ASSERT_EQUAL(WIDGET_REDRAW,
			  message_buffer_handle_event(&buf, MESSAGE_BUFFER_UP));
		} else {
			/* No previous message to check against. */
			check_select(&messages[i - 1], NULL);
		}

		message_buffer_redraw(&buf, &points);
	}

	for (size_t i = 0; i < size; i++) {
		size_t len = arrlenu(buf.buf);
		size_t scroll = buf.scroll;
		TEST_ASSERT_EQUAL(0, message_buffer_redact(&buf, messages[i].index));
		/* Ensure only 1 was deleted. */
		TEST_ASSERT_EQUAL(len - 1, arrlenu(buf.buf));
		/* Ensure the correct message was deleted. */
		TEST_ASSERT_EQUAL(-1, message_buffer_redact(&buf, messages[i].index));
		message_buffer_ensure_sane_scroll(&buf);

		if (i < (size - 1)) {
			TEST_ASSERT_EQUAL(scroll - 1, buf.scroll);
		}
	}

	/* Ensure empty buf. */
	TEST_ASSERT_EQUAL(0, arrlenu(buf.buf));
	TEST_ASSERT_EQUAL(
	  -1, message_buffer_redact(&buf, messages[size - 1].index));
}

void
test_actions(void) {
	TEST_ASSERT_EQUAL(
	  WIDGET_NOOP, message_buffer_handle_event(&buf, MESSAGE_BUFFER_UP));
	TEST_ASSERT_EQUAL(
	  WIDGET_NOOP, message_buffer_handle_event(&buf, MESSAGE_BUFFER_DOWN));
	TEST_ASSERT_EQUAL(WIDGET_NOOP,
	  message_buffer_handle_event(&buf, MESSAGE_BUFFER_SELECT, 25, 5));

	action();
	message_buffer_zero(&buf);
	TEST_ASSERT_EQUAL(0, arrlenu(buf.buf));
	TEST_ASSERT_TRUE(buf.zeroed);
	action();
	TEST_ASSERT_FALSE(buf.zeroed);
}

void
test_wrapping(void) {
	uint32_t *wrapped_bufs[] = {
	  buf_to_uint32_t(
		"nqjkdnqwjkdnqwdnqwjkdqwndjkqwndkjqwndkqwjndqwkjndqwjkddqwdqwt", 0),
	  buf_to_uint32_t(
		"asjkdnasdkjnsadsakdadkjsandsajkndaskjdaskjdnkjdqdwqfwqfqw\nqqwfqwngjkq"
		"engjkerwngjqngkjengjkqwgjkenwkjgqnwjkgnewqjkq😄\naflkasmfklqmgwqgqwghng"
		"jngbnjgfbfgbfgbgfew qfqw "
		"https://"
		"urlq\nfqwflqwmflqwqwqwfqwgqwjnkgqwjkgnkjgwnqjkgnqwgqwkjngwqkjngq🤔",
		0)};

	/* The exact values where we wrap. */
	const size_t start_end[][2] = {
	  {  0,  52},
	  { 52,  61},
	  {  0,  52},
	  { 52,  58},
	  { 58, 110},
	  {110, 116},
	  {116, 163},
	  {163, 176},
	  {176, 228},
	  {228, 235},
	};

	size_t len = sizeof(wrapped_bufs) / sizeof(*wrapped_bufs);

	for (size_t i = 0; i < len; i++) {
		messages[i].index = i;
		messages[i].body = wrapped_bufs[i];
		TEST_ASSERT_EQUAL(
		  0, message_buffer_insert(&buf, &points, &messages[i]));
	}

	size_t len_items = arrlenu(buf.buf);
	TEST_ASSERT_EQUAL(sizeof(start_end) / sizeof(*start_end), len_items);

	for (size_t i = 0; i < len_items; i++) {
		TEST_ASSERT_EQUAL(start_end[i][0], buf.buf[i].start);
		TEST_ASSERT_EQUAL(start_end[i][1], buf.buf[i].end);
	}

	for (size_t i = 0; i < len; i++) {
		arrfree(wrapped_bufs[i]);
	}
}

int
main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_actions);
	RUN_TEST(test_wrapping);
	return UNITY_END();
}
