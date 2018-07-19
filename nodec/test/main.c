#include <stdio.h>

#include <nodec.h>
#include <nodec-primitive.h>
#include <http_parser.h>
#include "request.h"

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


void http_in_print(http_in_t* in) {
  printf("headers: \n");
  size_t iter = 0;
  const char* value;
  const char* name;
  while ((name = http_in_header_next(in, &value, &iter)) != NULL) {
    printf(" %s: %s\n", name, value);
  }
  {with_buf(buf) {
    buf = async_http_in_read_body(in, 0);
    if (buf.base != NULL) {
      buf.base[buf.len] = 0;
      if (buf.len <= 80) {
        printf("body: %s\n", buf.base);
      }
      else {
        buf.base[30] = 0;
        printf("body: %s ... %s\n", buf.base, buf.base + buf.len - 30);
      }
    }
  }}
}


static void test_http_serve(int strand_id, http_in_t* in, http_out_t* out, lh_value arg) {
  fprintf(stderr, "strand %i entered\n", strand_id);
  // input
  printf("strand %i request, url: %s, content length: %llu\n", strand_id, http_in_url(in), http_in_content_length(in));
  http_in_print(in);

  // work
  printf("waiting %i secs...\n", 2 + strand_id); 
  async_wait(1000 + strand_id*1000);
  //check_uverr(UV_EADDRINUSE);

  // response
  http_out_add_header(out,"Content-Type","text/html; charset=utf-8");
  http_out_send_status_headers(out,HTTP_STATUS_OK,true);
  printf("request handled\n\n\n");
}

static void test_tcp() {
  define_ip4_addr("127.0.0.1", 8080,addr);
  async_http_server_at( addr, 0, 3, 0, &test_http_serve, lh_value_null );
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
Test scandir
-----------------------------------------------------------------*/

void test_scandir() {
  nodec_scandir_t* scan = async_scandir(".");
  {with_scandir(scan) {
    uv_dirent_t dirent;
    while (async_scandir_next(scan, &dirent)) {
      printf("entry %i: %s\n", dirent.type, dirent.name);
    }
  }}
}

/*-----------------------------------------------------------------
Test dns
-----------------------------------------------------------------*/

void test_dns() {
  struct addrinfo* info = async_getaddrinfo("iana.org", NULL, NULL);
  {with_addrinfo(info) {
    for (struct addrinfo* current = info; current != NULL; current = current->ai_next) {
      char sockname[128];
      nodec_sockname(current->ai_addr, sockname, sizeof(sockname));
      char* host = NULL;
      async_getnameinfo(current->ai_addr, 0, &host, NULL);
      {with_free(host) {
        printf("info: protocol %i at %s, reverse host: %s\n", current->ai_protocol, sockname, host);        
      }}
    }
  }}
}

/*-----------------------------------------------------------------
  Test connect
-----------------------------------------------------------------*/
const char* http_request =
  "GET / HTTP/1.1\r\n"
  "Host: www.bing.com\r\n"
  "Connection: close\r\n"
  "\r\n";

lh_value test_connection(http_in_t* in, http_out_t* out, lh_value arg) {
  http_out_add_header(out, "Connection", "close");
  http_out_send_request_headers(out, HTTP_GET, "/", true);
  async_http_in_read_headers(in); // wait for response
  printf("received, status: %i, content length: %llu\n", http_in_status(in), http_in_content_length(in));
  http_in_print(in);
  return lh_value_null;
}

void test_connect() {
  async_http_connect("www.bing.com", test_connection, lh_value_null);
}

/*-----------------------------------------------------------------
  Client Test
-----------------------------------------------------------------*/
const char* http_request_parts[] = {
  "GET / HTTP/1.1\r\n",
  "Host: 127.0.0.1\r\n",
  "Connection: close\r\n",
  "\r\n",
  NULL
};

void test_as_client() {
  uv_stream_t* conn = async_tcp_connect("127.0.0.1", "8080");
  {with_stream(conn) {
    const char* s;
    for (size_t i = 0; (s = http_request_parts[i]) != NULL; i++) {
      printf("write: %s\n", s);
      async_write(conn, s);
      async_wait(250);
    }
    printf("await response...\n");
    char* body = async_read_all(conn);
    {with_free(body) {
      printf("received:\n%s", body);
    }}
  }}
}

/*-----------------------------------------------------------------
 test url parsing
-----------------------------------------------------------------*/

static void url_print(const char* urlstr) {
  {with_url(urlstr, url) {
    printf("url: %s\n schema: %s\n userinfo: %s\n host: %s\n port: %u\n path: %s\n query: %s\n fragment: %s\n\n",
      urlstr,
      nodec_url_schema(url), nodec_url_userinfo(url), nodec_url_host(url), 
      nodec_url_port(url),
      nodec_url_path(url), nodec_url_query(url), nodec_url_fragment(url)
    );
  }}
}

static void host_url_print(const char* urlstr) {
  {with_host_url(urlstr, url) {
    printf("url: %s\n host: %s\n port: %u\n\n",
      urlstr, nodec_url_host(url), nodec_url_port(url)
    );
  }}
}

static void test_url() {
  url_print("http://daan@www.bing.com:72/foo?x=10;y=3#locallink");
  url_print("https://bing.com:8080");
  url_print("http://127.0.0.1");
  host_url_print("localhost:8080");
  host_url_print("my.server.com:80");
  host_url_print("127.0.0.1:80");
}

/*-----------------------------------------------------------------
  Main
-----------------------------------------------------------------*/

static void entry() {
  void test_http();
  printf("in the main loop\n");
  //test_files();
  //test_interleave();
  //test_cancel();
  //test_tcp();
  //test_tty_raw();
  //test_tty();
  //test_scandir();
  //test_dns();
  //test_http();
  //test_as_client();
  //test_connect();
  test_tcp_tty();
  //test_url();
}

int main() {
  async_main(entry);
  return 0;
}