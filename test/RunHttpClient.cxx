#include "lib/curl/Global.hxx"
#include "lib/curl/Init.hxx"
#include "lib/curl/Request.hxx"
#include "lib/curl/Handler.hxx"
#include "event/Loop.hxx"
#include "util/PrintException.hxx"

#include <fmt/core.h>

#include <cassert>

#include <stdlib.h>
#include <unistd.h>

static EventLoop event_loop;
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
	event_loop.Break();
	quit = true;
}

void
MyResponseHandler::OnHttpError(std::exception_ptr _error) noexcept
{
	error = _error;
	event_loop.Break();
	quit = true;
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fmt::print(stderr, "Usage: run_http_client URL\n");
		return EXIT_FAILURE;
	}

	const ScopeCurlInit init;
	CurlGlobal curl_global(event_loop, nullptr);

	const char *url = argv[1];

	MyResponseHandler handler;
	CurlRequest request(curl_global, url, {}, handler);
	if (!quit)
		event_loop.Run();
	assert(quit);

	if (error) {
		PrintException(error);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
