#include <stdio.h>
#include <nodec.h>
#include <nodec-primitive.h>
#include <http_parser.h>
#include "http_parser_wrapper.h"

/*-----------------------------------------------------------------
  Test files
-----------------------------------------------------------------*/
static void test_stat() {
  const char* path = "cenv.h";
  printf("stat file %s\n", path);
  uv_stat_t stat = async_stat(path);
  printf("file %s last access time: %li\n", path, stat.st_atim.tv_sec);
}

static void test_fileread() {
  const char* path = "cenv.h";
  printf("opening file: %s\n", path);
  char* contents = async_fread_full(path);
  {with_free(contents) {
    printf("read %Ii bytes from %s:\n...\n", strlen(contents), path);    
  }}
}

static void test_files() {
  test_stat();
  test_fileread();
}

/*-----------------------------------------------------------------
  Test interleave
-----------------------------------------------------------------*/

lh_value test_statx(lh_value arg) {
  test_stat();
  return lh_value_null;
}
lh_value test_filereadx(lh_value arg) {
  test_fileread();
  return lh_value_null;
}
lh_value test_filereads(lh_value arg) {
  printf("test filereads\n");
  lh_actionfun* actions[2] = { &test_filereadx, &test_statx };
  interleave(2, actions, NULL);
  return lh_value_null;
}

static void test_interleave() {
  lh_actionfun* actions[3] = { &test_filereadx, &test_statx, &test_filereads };
  interleave(3, actions, NULL);
}


/*-----------------------------------------------------------------
  Test cancel
-----------------------------------------------------------------*/

static lh_value test_cancel1(lh_value arg) {
  printf("starting work...\n");
  test_interleave();
  printf("and waiting a bit.. (1s)\n");
  async_wait(1000);
  printf("done work\n");
}

static void test_cancel_timeout(uint64_t timeout) {
  bool timedout = false;
  lh_value res = async_timeout(&test_cancel1, lh_value_null, timeout, &timedout);
  if (timedout) {
    printf("timed out\n");
  }
  else {
    printf("finished with: %i\n", lh_int_value(res));
  }
}

static void test_cancel() {
  test_cancel_timeout(1000);
  test_cancel_timeout(1500);
}


/*-----------------------------------------------------------------
  TCP
-----------------------------------------------------------------*/

const char* response_headers =
"HTTP/1.1 200 OK\r\n"
"Server : NodeC/0.1 (windows-x64)\r\n"
"Content-Type : text/html; charset=utf-8\r\n"
"Connection : Closed\r\n";

const char* response_body =
"<!DOCTYPE html>"
"<html>\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"</head>\n"
"<body>\n"
"  <h1>Hello NodeC World!</h1>\n"
"</body>\n"
"</html>\n";


static void test_http_serve(int strand_id, uv_stream_t* client) {
  fprintf(stderr, "strand %i entered\n", strand_id);
  // input
  const char* input = async_read(client);
  {with_free(input) {
    printf("strand %i received:%zi bytes\n%s", strand_id, strlen(input), input);
  }}
  // work
  printf("waiting %i secs...\n", 2 + strand_id); 
  async_wait(1000 + strand_id*1000);
  //check_uverr(UV_EADDRINUSE);

  // response
  {with_alloc_n(128, char, content_len) {
    snprintf(content_len, 128, "Content-Length: %zi\r\n\r\n", strlen(response_body));
    printf("strand %i: response body is %zi bytes\n", strand_id, strlen(response_body));
    const char* response[3] = { response_headers, content_len, response_body };
    async_write_strs(client, response, 3);
  }}
  printf("request handled\n\n\n");
}

static void test_tcp() {
  define_ip4_addr("127.0.0.1", 8080,addr);
  async_http_server_at( addr, 0, 3, 2500, &test_http_serve );
}


static lh_value test_tcpv(lh_value _arg) {
  test_tcp();
  return lh_value_null;
}

static lh_value test_ttyv(lh_value _arg) {
  {with_tty() {
    async_tty_write("press enter to quit the server...");
    const char* s = async_tty_readline();
    nodec_free(s);
    async_tty_write("canceling server...");
  }}
  return lh_value_null;
}

static void test_tcp_tty() {
  bool first;
  async_firstof(&test_tcpv, lh_value_null, &test_ttyv, lh_value_null, &first);
  // printf( first ? "http server exited\n" : "http server was terminated by the user\n");
}



/*-----------------------------------------------------------------
  TTY
-----------------------------------------------------------------*/
static void test_tty_raw() {
  uv_tty_t* tty_in = nodec_zero_alloc(uv_tty_t);
  {with_stream((uv_stream_t*)tty_in){
    nodec_check(uv_tty_init(async_loop(), tty_in, 0, true));
    nodec_read_start((uv_stream_t*)tty_in, 0, 255, 255);
    const char* s = async_read_line((uv_stream_t*)tty_in);
    {with_free(s) {
      printf("I got: %s\n", s);
    }}
    s = async_read_line((uv_stream_t*)tty_in);
    {with_free(s) {
      printf("Now I got: %s\n", s);
    }}
    printf("The end");
  }}
}

static void test_tty() {
  {with_tty() {
    async_tty_write("\033[41;37m");
    async_tty_write("what is your name? ");
    const char* s = async_tty_readline();
    {with_free(s) {
      printf("I got: %s\n", s);
    }}
    async_tty_write("and your age? ");
    s = async_tty_readline();
    {with_free(s) {
      printf("Now I got: %s\n", s);
    }}   
  }}
}

/*-----------------------------------------------------------------
Test HTTP
-----------------------------------------------------------------*/

int bVerbose = 1;

void init_settings(struct http_parser_settings* settings);

static const char* http_type_str(enum http_parser_type type)
{
	switch (type) {
	case HTTP_REQUEST: return "HTTP_REQUEST";
	case HTTP_RESPONSE: return "HTTP_RESPONSE";
	case HTTP_BOTH: return "HTTP_BOTH";
	default: return "UNKNOWN";
	}
}

static int test_chunk(struct http_parser *parser, const struct http_parser_settings *settings, const char* chunk) {
	size_t const len0 = strlen(chunk);
	size_t const len1 = HTTP_PARSER_EXECUTE(parser, settings, chunk, len0);
	if (parser->http_errno != HPE_OK || len0 != len1) {
		printf("Parsing Error\n");
		return 1;
	}
	else
		return 0;
}

static void test_chunks(const char* chunks[], size_t nchunks, enum http_parser_type type) {
	size_t i;
	struct http_parser_settings settings;
	struct http_parser parser;

	init_settings(&settings);
	HTTP_PARSER_INIT(&parser, type);
	for (i = 0; i < nchunks; i++) {
		if (test_chunk(&parser, &settings, chunks[i]) != 0)
			break;
	}
	printf("  type: %s\n", http_type_str(parser.type));
	if (parser.type == HTTP_REQUEST)
		printf("  method: %s\n", http_method_str(parser.method));
}


static void test_httpx_serve(int strand_id, uv_stream_t* client) {
	fprintf(stderr, "strand %i entered\n", strand_id);
	// input
	uv_buf_t buf = async_read_buf(client);
  if (buf.len > 0) {
    {with_free(buf.base) {
      buf.base[buf.len] = 0;
      printf("strand %i received:%u bytes\n%s\n", strand_id, buf.len, buf.base);
      const char* chunks[] = { buf.base };
      test_chunks(chunks, 1, HTTP_REQUEST);
    }}
  }
	// work
	printf("waiting %i secs...\n", 2 + strand_id);
	async_wait(1000 + strand_id * 1000);
	//check_uverr(UV_EADDRINUSE);

	// response
	{with_alloc_n(128, char, content_len) {
		snprintf(content_len, 128, "Content-Length: %zi\r\n\r\n", strlen(response_body));
		printf("strand %i: response body is %zi bytes\n", strand_id, strlen(response_body));
		const char* response[3] = { response_headers, content_len, response_body };
		async_write_strs(client, response, 3);
	}}
	printf("request handled\n\n\n");
}

static void test_http() {
	define_ip4_addr("127.0.0.1", 8080, addr);
	async_http_server_at(addr, 0, 3, 0, &test_httpx_serve);
}

/*-----------------------------------------------------------------
  Main
-----------------------------------------------------------------*/

static void entry() {
  printf("in the main loop\n");
  //test_files();
  //test_interleave();
  //test_cancel();
  //test_tcp();
  //test_tty_raw();
  //test_tty();
  //test_tcp_tty();
  test_http();
}

int main() {
  async_main(entry);
  return 0;
}