#include "HttpClient.hxx"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static GMainLoop *main_loop;
static GError *error;
static bool quit;

static void
my_response(size_t length, const char *data, G_GNUC_UNUSED void *ctx)
{
	write(STDOUT_FILENO, data, length);
	g_main_loop_quit(main_loop);
	quit = true;
}

static void
my_error(GError *_error, G_GNUC_UNUSED void *ctx)
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

	http_client_init();

	const char *url = argv[1];
	http_client_request(url, nullptr, &my_handler, nullptr);
	if (!quit)
		g_main_loop_run(main_loop);
	assert(quit);
	g_main_loop_unref(main_loop);

	http_client_finish();

	if (error != nullptr) {
		fprintf(stderr, "%s\n", error->message);
		g_error_free(error);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
