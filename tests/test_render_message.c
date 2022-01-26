#include "stb_ds.h"
#include "ui.h"
#include "unity.h"

void
setUp(void) {
}
void
tearDown(void) {
}

void
test_hash(void) {
	TEST_ASSERT_EQUAL(105, hsl_to_rgb(50, 20, 30));
	TEST_ASSERT_EQUAL(217, hsl_to_rgb(66, 53, 67));
	TEST_ASSERT_EQUAL(114, str_attr("test uintattr_t generation"));
}

void
test_buf(void) {
	const char *const bufs[]
	  = {"Testing 😄", "Test 🏳️‍🌈🏳️‍⚧️"};

	const uint32_t *const expected[]
	  = {L"Testing 😄", L"Test 🏳️‍🌈🏳️‍⚧️"};

	const size_t expected_lens[] = {9, 14};

	for (size_t i = 0; i < (sizeof(bufs) / sizeof(*bufs)); i++) {
		uint32_t *converted = buf_to_uint32_t(bufs[i], 0);
		uint32_t *converted_with_len
		  = buf_to_uint32_t(bufs[i], strlen(bufs[i]));

		TEST_ASSERT_EQUAL(expected_lens[i], arrlenu(converted));
		TEST_ASSERT_EQUAL_MEMORY(converted, expected[i], arrlenu(converted));
		TEST_ASSERT_EQUAL_MEMORY(
		  converted, converted_with_len, arrlenu(converted));

		arrfree(converted);
		arrfree(converted_with_len);
	}
}

void
test_mxid(void) {
	TEST_ASSERT_NULL(mxid_to_uint32_t(""));
	TEST_ASSERT_NULL(mxid_to_uint32_t("@x:"));

	const char *const bufs[] = {"@test:kde.org", "@😄asdf:localhost"};

	const uint32_t *const expected[] = {
	  L"test",
	  L"😄asdf",
	};

	for (size_t i = 0; i < (sizeof(bufs) / sizeof(*bufs)); i++) {
		uint32_t *buf = mxid_to_uint32_t(bufs[i]);
		TEST_ASSERT_NOT_NULL(buf);
		TEST_ASSERT_EQUAL_MEMORY(buf, expected[i], arrlenu(buf));
		arrfree(buf);
	}
}

int
main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_hash);
	RUN_TEST(test_buf);
	RUN_TEST(test_mxid);
	return UNITY_END();
}
