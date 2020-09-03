#include "lib/curl/Global.hxx"
#include "lib/curl/Request.hxx"
#include "lib/curl/Handler.hxx"
#include "util/PrintException.hxx"

#include <boost/asio/io_service.hpp>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static boost::asio::io_service io_service;
static std::exception_ptr error;
static bool quit;

class MyResponseHandler final : public HttpResponseHandler {
public:
	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(std::string body) noexcept override;
	void OnHttpError(std::exception_ptr e) noexcept override;
};

void
MyResponseHandler::OnHttpResponse(std::string body) noexcept
{
	write(STDOUT_FILENO, body.data(), body.size());
	io_service.stop();
	quit = true;
}

void
MyResponseHandler::OnHttpError(std::exception_ptr _error) noexcept
{
	error = _error;
	io_service.stop();
	quit = true;
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: run_http_client URL\n");
		return EXIT_FAILURE;
	}

	CurlGlobal curl_global(io_service, nullptr);

	const char *url = argv[1];

	MyResponseHandler handler;
	CurlRequest request(curl_global, url, {}, handler);
	if (!quit)
		io_service.run();
	assert(quit);

	if (error) {
		PrintException(error);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
