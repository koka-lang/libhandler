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
  interleave(2, actions);
  return lh_value_null;
}

static void test_interleave() {
  lh_actionfun* actions[3] = { &test_filereadx, &test_statx, &test_filereads };
  interleave(3, actions);
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

uv_stream_t* tcp_channel_receive(tcp_channel_t* ch) {
  channel_elem e = channel_receive(ch);
  //printf("got a connection!\n");
  return (uv_stream_t*)lh_ptr_value(e.data);
}

void async_shutdownv(lh_value streamv) {
  async_shutdown((uv_stream_t*)lh_ptr_value(streamv));
}

#define with_stream(s)  defer(async_shutdownv,lh_value_ptr(s))

char* async_read_str(uv_stream_t* stream, ssize_t max_len, ssize_t* nread ) {
  // TODO: we should keep reading until `max_len` or EOF, and realloc
  // slowly in case max_len is very large, and realloc eventually if `nread`<`max_len`.
  uv_buf_t buf = uv_buf_init(nodec_nalloc(max_len+1, char), (ULONG)max_len);
  {on_exn(nodec_freev, lh_value_ptr(buf.base)) {
    ssize_t n = async_read(stream, buf, 0);
    buf.base[n] = 0;
    if (nread!=NULL) *nread = n;
  }}
  return buf.base;
}

static void test_tcp() {
  tcp_channel_t* ch = nodec_tcp_listen_at4("127.0.0.1", 8080, 0, 0);
  {defer(channel_freev, lh_value_ptr(ch)) {
    int max_connects = 3;
    while (max_connects-- > 0) {
      uv_stream_t* client = tcp_channel_receive(ch);
      {with_stream(client){
        // input
        const char* input = async_read_str(client, 1024, NULL);
        {with_free(input){
          printf("received:%i bytes\n%s\n", strlen(input), input);
        }}
        // response: todo: we cannot stack allocate write buffers in general.
        // it goes ok here because if fits in the initial low level write buffer (of 64kb)
        char content_len[128];
        snprintf(content_len, 128, "Content-Length: %i\r\n\r\n", strlen(response_body));
        const char* response[3] = { response_headers, content_len, response_body };
        async_write_strs(client, response, 3);
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
  test_tcp();
}


int main() {
  async_main(entry);
  return 0;
}