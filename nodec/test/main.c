#include <stdio.h>
#include <nodec.h>

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


static void test_tcp_serve(int strand_id, uv_stream_t* client) {
  fprintf(stderr, "strand %i entered\n", strand_id);
  // input
  const char* input = async_read_chunk(client, 1024, NULL);
  {with_free(input) {
    printf("strand %i received:%zi bytes\n%s", strand_id, strlen(input), input);
  }}
  // work
  printf("waiting %i secs...\n", 3 + strand_id); 
  async_delay(3000 + strand_id*1000);

  // response
  {with_nalloc(128, char, content_len) {
    snprintf(content_len, 128, "Content-Length: %zi\r\n\r\n", strlen(response_body));
    printf("strand %i: response body is %zi bytes\n", strand_id, strlen(response_body));
    const char* response[3] = { response_headers, content_len, response_body };
    async_write_strs(client, response, 3);
  }}
  printf("request handled\n\n\n");
}

static void test_tcp() {
  define_ip4_addr("127.0.0.1", 8080,addr);
  async_tcp_server_at( addr, 0, 0, 3, &test_tcp_serve );
}

static void test_tcp_raw() {
  define_ip4_addr("127.0.0.1", 8080, addr);
  tcp_channel_t* ch = nodec_tcp_listen_at(addr, 0, 0);
  {with_tcp_channel(ch){
    int max_connects = 3;
    while (max_connects-- > 0) {
      uv_stream_t* client = tcp_channel_receive(ch);
      {with_stream(client){
        test_tcp_serve(max_connects, client);
      }}
    }
  }}
}


/*-----------------------------------------------------------------
  Main
-----------------------------------------------------------------*/

static void entry() {
  printf("in the main loop\n");
  //test_files();
  //test_interleave();
  //test_tcp_raw();
  test_tcp();
}


int main() {
  async_main(entry);
  return 0;
}