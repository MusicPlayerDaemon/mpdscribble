#include "HttpClient.hxx"
#include "util/PrintException.hxx"

#include <glib.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static GMainLoop *main_loop;
static std::exception_ptr error;
static bool quit;

static void
my_response(std::string body, void *)
{
	write(STDOUT_FILENO, body.data(), body.size());
	g_main_loop_quit(main_loop);
	quit = true;
}

static void
my_error(std::exception_ptr _error, void *)
{
	error = _error;
	g_main_loop_quit(main_loop);
	quit = true;
}

static constexpr HttpClientHandler my_handler = {
	.response = my_response,
	.error = my_error,
};

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: run_http_client URL\n");
		return EXIT_FAILURE;
	}

	main_loop = g_main_loop_new(nullptr, false);

	HttpClient http_client;

	const char *url = argv[1];
	HttpRequest request(http_client, url, {}, my_handler, nullptr);
	if (!quit)
		g_main_loop_run(main_loop);
	assert(quit);
	g_main_loop_unref(main_loop);

	if (error) {
		PrintException(error);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
