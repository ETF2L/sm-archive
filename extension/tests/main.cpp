/* Copyright 2026 ETF2L Contributors, Dave
 *
 * Runs every registered test in a fresh directory below the system's temp
 * directory:
 *   smarchive-tests [fixtures]
 * fixtures is the directory of the generated hostile archives, the RAR
 * files and the interop files (tests/fixtures/); without it the tests that
 * need it are skipped. */
#include "check.h"
#include "support.h"

#include <stdlib.h>
#include <unistd.h>

#include <filesystem>

namespace tests {

std::vector<TestCase> &Registry()
{
	static std::vector<TestCase> registry;
	return registry;
}

int &Failures()
{
	static int failures = 0;
	return failures;
}

} // namespace tests

int main(int argc, char **argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc > 1)
		tests::SetFixtures(argv[1]);
	std::filesystem::path root = std::filesystem::temp_directory_path() / ("smarchive-tests-" + std::to_string(getpid()));
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);

	for (const tests::TestCase &test : tests::Registry()) {
		std::filesystem::path directory = root / test.name;
		std::filesystem::create_directories(directory);
		tests::SetWorkDirectory(directory.string());
		int before = tests::Failures();
		test.body();
		printf("%-48s %s\n", test.name, tests::Failures() == before ? "ok" : "FAILED");
	}
	std::filesystem::remove_all(root);
	printf("%d check(s) failed\n", tests::Failures());
	return tests::Failures() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
