/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The tests' checks: a failed check is printed and counted, the program
 * exits non-zero when any failed. Tests register themselves with TEST(). */
#ifndef SMARCHIVE_TESTS_CHECK_H
#define SMARCHIVE_TESTS_CHECK_H

#include "status.h"

#include <stdio.h>

#include <functional>
#include <string>
#include <vector>

namespace tests {

struct TestCase {
	const char *name;
	std::function<void()> body;
};

std::vector<TestCase> &Registry();
int &Failures();

struct Registrar {
	Registrar(const char *name, std::function<void()> body) { Registry().push_back(TestCase{name, std::move(body)}); }
};

} // namespace tests

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)
#define TEST(name)                                                                                  \
	static void name();                                                                             \
	static tests::Registrar TEST_CONCAT(registrar_, name)(#name, name);                             \
	static void name()

#define CHECK(condition)                                                                            \
	do {                                                                                            \
		if (!(condition)) {                                                                         \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);           \
			tests::Failures()++;                                                                    \
		}                                                                                           \
	} while (0)

#define CHECK_CODE(status, expected)                                                                \
	do {                                                                                            \
		smarchive::Status checked_ = (status);                                                      \
		smarchive::ErrorCode expected_ = (expected);                                                \
		if (checked_.code != expected_) {                                                           \
			fprintf(stderr, "%s:%d: expected %s, got %s (%s)\n", __FILE__, __LINE__,                \
			        smarchive::ErrorCodeName(expected_), smarchive::ErrorCodeName(checked_.code),   \
			        checked_.message.c_str());                                                      \
			tests::Failures()++;                                                                    \
		}                                                                                           \
	} while (0)

#define CHECK_OK(status) CHECK_CODE(status, smarchive::ErrorCode::None)

#endif // SMARCHIVE_TESTS_CHECK_H
