#include "ui/login_form.h"

#include "unity.h"

static struct form form = {0};
static struct input *last_field = NULL;

void
setUp(void) {
	form_init(&form, TB_DEFAULT);
}

void
tearDown(void) {
	form_finish(&form);
}

static void
ensure_button(enum field_buttons button) {
	TEST_ASSERT_EQUAL(true, form.button_is_selected);
	TEST_ASSERT_EQUAL(button, form.current_button);
	TEST_ASSERT_NULL(form_current_input(&form));
}

static void
ensure_field(enum field field) {
	TEST_ASSERT_EQUAL(false, form.button_is_selected);
	TEST_ASSERT_EQUAL(field, form.current_field);
	struct input *tmp = form_current_input(&form);
	TEST_ASSERT_NOT_NULL(tmp);
	TEST_ASSERT_FALSE(tmp == last_field);
	last_field = tmp;
}

void
test_login_form(void) {
	TEST_ASSERT_EQUAL(WIDGET_NOOP, form_handle_event(&form, FORM_UP));

	/* Scroll to buttons. */
	for (size_t i = 0; i < FIELD_MAX; i++) {
		ensure_field(i);
		TEST_ASSERT_EQUAL(WIDGET_REDRAW, form_handle_event(&form, FORM_DOWN));
	}

	ensure_button(FIELD_BUTTON_LOGIN);
	TEST_ASSERT_EQUAL(WIDGET_REDRAW, form_handle_event(&form, FORM_DOWN));

	ensure_button(FIELD_BUTTON_REGISTER);

	/* At the bottom. */
	TEST_ASSERT_EQUAL(WIDGET_NOOP, form_handle_event(&form, FORM_DOWN));

	TEST_ASSERT_EQUAL(WIDGET_REDRAW, form_handle_event(&form, FORM_UP));
	ensure_button(FIELD_BUTTON_LOGIN);

	last_field = NULL;

	for (size_t i = 0; i < FIELD_MAX; i++) {
		TEST_ASSERT_EQUAL(WIDGET_REDRAW, form_handle_event(&form, FORM_UP));
		ensure_field(FIELD_MAX - 1 - i);
	}

	TEST_ASSERT_EQUAL(WIDGET_NOOP, form_handle_event(&form, FORM_UP));

	struct widget_points points = {.x1 = 0, .x2 = 80, .y1 = 0, .y2 = 24};

	form_redraw(&form, &points);
}

int
main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_login_form);
	return UNITY_END();
}
