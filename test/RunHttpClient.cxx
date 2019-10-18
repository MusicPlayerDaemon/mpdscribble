#include "lib/curl/Global.hxx"
#include "lib/curl/Request.hxx"
#include "util/PrintException.hxx"

#include <glib.h>

#include <boost/asio/io_service.hpp>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static boost::asio::io_service io_service;
static std::exception_ptr error;
static bool quit;

static void
my_response(std::string body, void *)
{
	write(STDOUT_FILENO, body.data(), body.size());
	io_service.stop();
	quit = true;
}

static void
my_error(std::exception_ptr _error, void *)
{
	error = _error;
	io_service.stop();
	quit = true;
}

static constexpr HttpResponseHandler my_handler = {
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

	CurlGlobal curl_global(io_service);

	const char *url = argv[1];
	HttpRequest request(curl_global, url, {}, my_handler, nullptr);
	if (!quit)
		io_service.run();
	assert(quit);

	if (error) {
		PrintException(error);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
