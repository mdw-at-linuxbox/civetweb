/* Copyright (c) 2015 the Civetweb developers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "public_server.h"
#include <civetweb.h>

#if defined(_WIN32)
#include <Windows.h>
#define test_sleep(x) (Sleep(x * 1000))
#else
#include <unistd.h>
#define test_sleep(x) (sleep(x))
#endif

/* This unit test file uses the excellent Check unit testing library.
 * The API documentation is available here:
 * http://check.sourceforge.net/doc/check_html/index.html
 */

static const char *
locate_resources(void)
{
	char *cp;

	cp = getenv("TEST_CERT_DIR");
	if (cp)
		return cp;
	return
#ifdef _WIN32
#ifdef LOCAL_TEST
	    "resources\\";
#else
	    /* Appveyor */
	    ".\\"; /* TODO: the different paths
	                                            * used in the different test
	                                            * system is an unsolved
	                                            * problem */
#endif
#else
#ifdef LOCAL_TEST
	    "resources/";
#else
	    /* Travis */
	    "./"; // TODO: fix path in CI test environment
#endif
#endif
}


static const char *
locate_ssl_cert(void)
{
	static char cert_path[256];
	const char *res = locate_resources();
	size_t l;

	ck_assert(res != NULL);
	l = strlen(res);
	ck_assert_uint_gt(l, 0);
	ck_assert_uint_lt(l, 100); /* assume there is enough space left in our
	                              typical 255 character string buffers */

	strcpy(cert_path, res);
	strcat(cert_path, "ssl_cert.pem");
	return cert_path;
}


static int
wait_not_null(void *volatile *data)
{
	int i;
	for (i = 0; i < 100; i++) {
		test_sleep(1);
		if (*data != NULL) {
			return 1;
		}
	}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunreachable-code"
#endif

	ck_abort_msg("wait_not_null failed");

	return 0;

#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

START_TEST(test_the_test_environment)
{
	char wd[300];
	char buf[500];
	FILE *f;
	struct stat st;
	int ret;
	const char *ssl_cert = locate_ssl_cert();

	memset(wd, 0, sizeof(wd));
	memset(buf, 0, sizeof(buf));

/* Get the current working directory */
#ifdef _WIN32
	(void)GetCurrentDirectoryA(sizeof(wd), wd);
	wd[sizeof(wd) - 1] = 0;
#else
	(void)getcwd(wd, sizeof(wd));
	wd[sizeof(wd) - 1] = 0;
#endif

/* Check the pem file */
#ifdef _WIN32
	strcpy(buf, wd);
	strcat(buf, "\\");
	strcat(buf, ssl_cert);
	f = fopen(buf, "rb");
#else
	strcpy(buf, wd);
	strcat(buf, "/");
	strcat(buf, ssl_cert);
	f = fopen(buf, "r");
#endif

	if (f) {
		fclose(f);
	} else {
		fprintf(stderr, "%s not found", buf);
	}

/* Check the test dir */
#ifdef _WIN32
	strcpy(buf, wd);
	strcat(buf, "\\test");
#else
	strcpy(buf, wd);
	strcat(buf, "/test");
#endif

	memset(&st, 0, sizeof(st));
	ret = stat(buf, &st);

	if (ret) {
		fprintf(stderr, "%s not found", buf);
	}
}
END_TEST


static void *threading_data;

static void *
test_thread_func_t(void *param)
{
	ck_assert_ptr_eq(param, &threading_data);
	ck_assert_ptr_eq(threading_data, NULL);
	threading_data = &threading_data;
	return NULL;
}

START_TEST(test_threading)
{
	int ok;

	threading_data = NULL;

	ok = mg_start_thread(test_thread_func_t, &threading_data);
	ck_assert_int_eq(ok, 0);

	wait_not_null(&threading_data);
	ck_assert_ptr_eq(threading_data, &threading_data);
}
END_TEST


static int
log_msg_func(const struct mg_connection *conn, const char *message)
{
	struct mg_context *ctx;
	char *ud;

	ck_assert(conn != NULL);
	ctx = mg_get_context(conn);
	ck_assert(ctx != NULL);
	ud = (char *)mg_get_user_data(ctx);

	strncpy(ud, message, 255);
	ud[255] = 0;
	return 1;
}

START_TEST(test_mg_start_stop_http_server)
{
	struct mg_context *ctx;
	const char *OPTIONS[] = {
#if !defined(NO_FILES)
		"document_root",
		".",
#endif
		"listening_ports",
		"8080",
		NULL,
	};
	size_t ports_cnt;
	int ports[16];
	int ssl[16];
	struct mg_callbacks callbacks;
	char errmsg[256];

	struct mg_connection *client_conn;
	char client_err[256];
	const struct mg_request_info *client_ri;
	int client_res;

	memset(ports, 0, sizeof(ports));
	memset(ssl, 0, sizeof(ssl));
	memset(&callbacks, 0, sizeof(callbacks));
	memset(errmsg, 0, sizeof(errmsg));

	callbacks.log_message = log_msg_func;

	ctx = mg_start(&callbacks, (void *)errmsg, OPTIONS);
	test_sleep(1);
	ck_assert_str_eq(errmsg, "");
	ck_assert(ctx != NULL);

	ports_cnt = mg_get_ports(ctx, 16, ports, ssl);
	ck_assert_uint_eq(ports_cnt, 1);
	ck_assert_int_eq(ports[0], 8080);
	ck_assert_int_eq(ssl[0], 0);
	ck_assert_int_eq(ports[1], 0);
	ck_assert_int_eq(ssl[1], 0);

	test_sleep(1);

	memset(client_err, 0, sizeof(client_err));
	client_conn =
	    mg_connect_client("127.0.0.1", 8080, 0, client_err, sizeof(client_err));
	ck_assert(client_conn != NULL);
	ck_assert_str_eq(client_err, "");
	mg_printf(client_conn, "GET / HTTP/1.0\r\n\r\n");
	client_res =
	    mg_get_response(client_conn, client_err, sizeof(client_err), 10000);
	ck_assert_int_ge(client_res, 0);
	ck_assert_str_eq(client_err, "");
	client_ri = mg_get_request_info(client_conn);
	ck_assert(client_ri != NULL);

#if defined(NO_FILES)
	ck_assert_str_eq(client_ri->uri, "404");
#else
	ck_assert_str_eq(client_ri->uri, "200");
	/* TODO: ck_assert_str_eq(client_ri->request_method, "HTTP/1.0"); */
	client_res = (int)mg_read(client_conn, client_err, sizeof(client_err));
	ck_assert_int_gt(client_res, 0);
	ck_assert_int_le(client_res, sizeof(client_err));
#endif
	mg_close_connection(client_conn);

	test_sleep(1);

	mg_stop(ctx);
}
END_TEST


START_TEST(test_mg_start_stop_https_server)
{
#ifndef NO_SSL

	struct mg_context *ctx;

	size_t ports_cnt;
	int ports[16];
	int ssl[16];
	struct mg_callbacks callbacks;
	char errmsg[256];

	const char *OPTIONS[8]; /* initializer list here is rejected by CI test */
	int opt_idx = 0;
	const char *ssl_cert = locate_ssl_cert();

	struct mg_connection *client_conn;
	char client_err[256];
	const struct mg_request_info *client_ri;
	int client_res;

	ck_assert(ssl_cert != NULL);

	memset((void *)OPTIONS, 0, sizeof(OPTIONS));
#if !defined(NO_FILES)
	OPTIONS[opt_idx++] = "document_root";
	OPTIONS[opt_idx++] = ".";
#endif
	OPTIONS[opt_idx++] = "listening_ports";
	OPTIONS[opt_idx++] = "8080r,8443s";
	OPTIONS[opt_idx++] = "ssl_certificate";
	OPTIONS[opt_idx++] = ssl_cert;

	ck_assert_int_le(opt_idx, (int)(sizeof(OPTIONS) / sizeof(OPTIONS[0])));
	ck_assert(OPTIONS[sizeof(OPTIONS) / sizeof(OPTIONS[0]) - 1] == NULL);
	ck_assert(OPTIONS[sizeof(OPTIONS) / sizeof(OPTIONS[0]) - 2] == NULL);

	memset(ports, 0, sizeof(ports));
	memset(ssl, 0, sizeof(ssl));
	memset(&callbacks, 0, sizeof(callbacks));
	memset(errmsg, 0, sizeof(errmsg));

	callbacks.log_message = log_msg_func;

	ctx = mg_start(&callbacks, (void *)errmsg, OPTIONS);
	test_sleep(1);
	ck_assert_str_eq(errmsg, "");
	ck_assert(ctx != NULL);

	ports_cnt = mg_get_ports(ctx, 16, ports, ssl);
	ck_assert_uint_eq(ports_cnt, 2);
	ck_assert_int_eq(ports[0], 8080);
	ck_assert_int_eq(ssl[0], 0);
	ck_assert_int_eq(ports[1], 8443);
	ck_assert_int_eq(ssl[1], 1);
	ck_assert_int_eq(ports[2], 0);
	ck_assert_int_eq(ssl[2], 0);

	test_sleep(1);

	memset(client_err, 0, sizeof(client_err));
	client_conn =
	    mg_connect_client("127.0.0.1", 8443, 1, client_err, sizeof(client_err));
	ck_assert(client_conn != NULL);
	ck_assert_str_eq(client_err, "");
	mg_printf(client_conn, "GET / HTTP/1.0\r\n\r\n");
	client_res =
	    mg_get_response(client_conn, client_err, sizeof(client_err), 10000);
	ck_assert_int_ge(client_res, 0);
	ck_assert_str_eq(client_err, "");
	client_ri = mg_get_request_info(client_conn);
	ck_assert(client_ri != NULL);

#if defined(NO_FILES)
	ck_assert_str_eq(client_ri->uri, "404");
#else
	ck_assert_str_eq(client_ri->uri, "200");
	/* TODO: ck_assert_str_eq(client_ri->request_method, "HTTP/1.0"); */
	client_res = (int)mg_read(client_conn, client_err, sizeof(client_err));
	ck_assert_int_gt(client_res, 0);
	ck_assert_int_le(client_res, sizeof(client_err));
#endif
	mg_close_connection(client_conn);

	test_sleep(1);

	mg_stop(ctx);
#endif
}
END_TEST


START_TEST(test_mg_server_and_client_tls)
{
#ifndef NO_SSL

	struct mg_context *ctx;

	int ports_cnt;
	struct mg_server_ports ports[16];
	struct mg_callbacks callbacks;
	char errmsg[256];

	struct mg_connection *client_conn;
	char client_err[256];
	const struct mg_request_info *client_ri;
	int client_res;
	struct mg_client_options client_options;

	const char *OPTIONS[32]; /* initializer list here is rejected by CI test */
	int opt_idx = 0;
	char server_cert[256];
	char client_cert[256];
	const char *res_dir = locate_resources();
	char *listening_ports_override;

	ck_assert(res_dir != NULL);
	strcpy(server_cert, res_dir);
	strcpy(client_cert, res_dir);
#ifdef _WIN32
	strcat(server_cert, "cert\\server.pem");
	strcat(client_cert, "cert\\client.pem");
#else
	strcat(server_cert, "cert/server.pem");
	strcat(client_cert, "cert/client.pem");
#endif

	memset((void *)OPTIONS, 0, sizeof(OPTIONS));
#if !defined(NO_FILES)
	OPTIONS[opt_idx++] = "document_root";
	OPTIONS[opt_idx++] = ".";
#endif
	listening_ports_override = getenv("TEST_PORTS");
	OPTIONS[opt_idx++] = "listening_ports";
	OPTIONS[opt_idx++] =
	    listening_ports_override ? listening_ports_override : "8080r,8443s";
	OPTIONS[opt_idx++] = "ssl_certificate";
	OPTIONS[opt_idx++] = server_cert;
	OPTIONS[opt_idx++] = "ssl_verify_peer";
	OPTIONS[opt_idx++] = "yes";
	OPTIONS[opt_idx++] = "ssl_ca_file";
	OPTIONS[opt_idx++] = client_cert;

	ck_assert_int_le(opt_idx, (int)(sizeof(OPTIONS) / sizeof(OPTIONS[0])));
	ck_assert(OPTIONS[sizeof(OPTIONS) / sizeof(OPTIONS[0]) - 1] == NULL);
	ck_assert(OPTIONS[sizeof(OPTIONS) / sizeof(OPTIONS[0]) - 2] == NULL);

	memset(ports, 0, sizeof(ports));
	memset(&callbacks, 0, sizeof(callbacks));
	memset(errmsg, 0, sizeof(errmsg));

	callbacks.log_message = log_msg_func;

	ctx = mg_start(&callbacks, (void *)errmsg, OPTIONS);
	test_sleep(1);
	ck_assert_str_eq(errmsg, "");
	ck_assert(ctx != NULL);

	ports_cnt = mg_get_server_ports(ctx, 16, ports);
	ck_assert_int_eq(ports_cnt, 2);
	ck_assert_int_eq(ports[0].protocol, 1);
	ck_assert_int_eq(ports[0].port, 8080);
	ck_assert_int_eq(ports[0].is_ssl, 0);
	ck_assert_int_eq(ports[0].is_redirect, 1);
	ck_assert_int_eq(ports[1].protocol, 1);
	ck_assert_int_eq(ports[1].port, 8443);
	ck_assert_int_eq(ports[1].is_ssl, 1);
	ck_assert_int_eq(ports[1].is_redirect, 0);
	ck_assert_int_eq(ports[2].protocol, 0);
	ck_assert_int_eq(ports[2].port, 0);
	ck_assert_int_eq(ports[2].is_ssl, 0);
	ck_assert_int_eq(ports[2].is_redirect, 0);

	test_sleep(1);

	memset(client_err, 0, sizeof(client_err));
	client_conn =
	    mg_connect_client("127.0.0.1", 8443, 1, client_err, sizeof(client_err));
	ck_assert(client_conn == NULL);
	ck_assert_str_ne(client_err, "");

	memset(client_err, 0, sizeof(client_err));
	memset(&client_options, 0, sizeof(client_options));
	client_options.host = "127.0.0.1";
	client_options.port = 8443;
	client_options.client_cert = client_cert;
	client_options.server_cert = server_cert;

	client_conn = mg_connect_client_secure(&client_options,
	                                       client_err,
	                                       sizeof(client_err));
	ck_assert(client_conn != NULL);
	ck_assert_str_eq(client_err, "");
	mg_printf(client_conn, "GET / HTTP/1.0\r\n\r\n");
	client_res =
	    mg_get_response(client_conn, client_err, sizeof(client_err), 10000);
	ck_assert_int_ge(client_res, 0);
	ck_assert_str_eq(client_err, "");
	client_ri = mg_get_request_info(client_conn);
	ck_assert(client_ri != NULL);

#if defined(NO_FILES)
	ck_assert_str_eq(client_ri->uri, "404");
#else
	ck_assert_str_eq(client_ri->uri, "200");
	/* TODO: ck_assert_str_eq(client_ri->request_method, "HTTP/1.0"); */
	client_res = (int)mg_read(client_conn, client_err, sizeof(client_err));
	ck_assert_int_gt(client_res, 0);
	ck_assert_int_le(client_res, sizeof(client_err));
#endif
	mg_close_connection(client_conn);

	/* TODO: A client API using a client certificate is missing */

	test_sleep(1);

	mg_stop(ctx);
#endif
}
END_TEST


static struct mg_context *g_ctx;

static int
request_test_handler(struct mg_connection *conn, void *cbdata)
{
	int i;
	char chunk_data[32];
	const struct mg_request_info *ri;
	struct mg_context *ctx;
	void *ud, *cud;

	ctx = mg_get_context(conn);
	ud = mg_get_user_data(ctx);
	ri = mg_get_request_info(conn);

	ck_assert(ri != NULL);
	ck_assert(ctx == g_ctx);
	ck_assert(ud == &g_ctx);

	mg_set_user_connection_data(conn, (void *)6543);
	cud = mg_get_user_connection_data(conn);
	ck_assert_ptr_eq((void *)cud, (void *)6543);

	ck_assert_ptr_eq((void *)cbdata, (void *)7);
	strcpy(chunk_data, "123456789A123456789B123456789C");

	mg_printf(conn,
	          "HTTP/1.1 200 OK\r\n"
	          "Transfer-Encoding: chunked\r\n"
	          "Content-Type: text/plain\r\n\r\n");

	for (i = 1; i <= 10; i++) {
		mg_printf(conn, "%x\r\n", i);
		mg_write(conn, chunk_data, (unsigned)i);
		mg_printf(conn, "\r\n");
	}

	mg_printf(conn, "0\r\n\r\n");

	return 1;
}

#ifdef USE_WEBSOCKET
/****************************************************************************/
/* WEBSOCKET SERVER                                                         */
/****************************************************************************/
static const char *websocket_welcome_msg = "websocket welcome\n";
static const size_t websocket_welcome_msg_len =
    18 /* strlen(websocket_welcome_msg) */;
static const char *websocket_goodbye_msg = "websocket bye\n";
static const size_t websocket_goodbye_msg_len =
    14 /* strlen(websocket_goodbye_msg) */;

static int
websock_server_connect(const struct mg_connection *conn, void *udata)
{
	(void)conn;

	ck_assert_ptr_eq((void *)udata, (void *)7531);
	printf("Server: Websocket connected\n");

	return 0; /* return 0 to accept every connection */
}

static void
websock_server_ready(struct mg_connection *conn, void *udata)
{
	ck_assert_ptr_eq((void *)udata, (void *)7531);
	printf("Server: Websocket ready\n");

	/* Send websocket welcome message */
	mg_lock_connection(conn);
	mg_websocket_write(conn,
	                   WEBSOCKET_OPCODE_TEXT,
	                   websocket_welcome_msg,
	                   websocket_welcome_msg_len);
	mg_unlock_connection(conn);

	printf("Server: Websocket ready X\n");
}

static int
websock_server_data(struct mg_connection *conn,
                    int bits,
                    char *data,
                    size_t data_len,
                    void *udata)
{
	(void)bits;

	ck_assert_ptr_eq((void *)udata, (void *)7531);
	printf("Server: Got %u bytes from the client\n", (unsigned)data_len);

	if (data_len == 3 && !memcmp(data, "bye", 3)) {
		/* Send websocket goodbye message */
		mg_lock_connection(conn);
		mg_websocket_write(conn,
		                   WEBSOCKET_OPCODE_TEXT,
		                   websocket_goodbye_msg,
		                   websocket_goodbye_msg_len);
		mg_unlock_connection(conn);
	} else if (data_len == 5 && !memcmp(data, "data1", 5)) {
		mg_lock_connection(conn);
		mg_websocket_write(conn, WEBSOCKET_OPCODE_TEXT, "ok1", 3);
		mg_unlock_connection(conn);
	} else if (data_len == 5 && !memcmp(data, "data2", 5)) {
		mg_lock_connection(conn);
		mg_websocket_write(conn, WEBSOCKET_OPCODE_TEXT, "ok 2", 4);
		mg_unlock_connection(conn);
	} else if (data_len == 5 && !memcmp(data, "data3", 5)) {
		mg_lock_connection(conn);
		mg_websocket_write(conn, WEBSOCKET_OPCODE_TEXT, "ok - 3", 6);
		mg_unlock_connection(conn);
	} else {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunreachable-code"
#endif

		ck_abort_msg("Got unexpected message from websocket client");


		return 0;

#ifdef __clang__
#pragma clang diagnostic pop
#endif
	}

	return 1; /* return 1 to keep the connetion open */
}

static void
websock_server_close(const struct mg_connection *conn, void *udata)
{
	(void)conn;

	ck_assert_ptr_eq((void *)udata, (void *)7531);
	printf("Server: Close connection\n");

	/* Can not send a websocket goodbye message here - the connection is already
	 * closed */
}

/****************************************************************************/
/* WEBSOCKET CLIENT                                                         */
/****************************************************************************/
struct tclient_data {
	void *data;
	size_t len;
	int closed;
};

static int
websocket_client_data_handler(struct mg_connection *conn,
                              int flags,
                              char *data,
                              size_t data_len,
                              void *user_data)
{
	struct mg_context *ctx = mg_get_context(conn);
	struct tclient_data *pclient_data =
	    (struct tclient_data *)mg_get_user_data(ctx);

	(void)user_data; /* TODO: check this */

	ck_assert(pclient_data != NULL);
	ck_assert_int_eq(flags, (int)(128 | 1));

	printf("Client received data from server: ");
	fwrite(data, 1, data_len, stdout);
	printf("\n");

	pclient_data->data = malloc(data_len);
	ck_assert(pclient_data->data != NULL);
	memcpy(pclient_data->data, data, data_len);
	pclient_data->len = data_len;

	return 1;
}

static void
websocket_client_close_handler(const struct mg_connection *conn,
                               void *user_data)
{
	struct mg_context *ctx = mg_get_context(conn);
	struct tclient_data *pclient_data =
	    (struct tclient_data *)mg_get_user_data(ctx);

	(void)user_data; /* TODO: check this */

	ck_assert(pclient_data != NULL);

	printf("Client: Close handler\n");
	pclient_data->closed++;
}
#endif

START_TEST(test_request_handlers)
{
	char ebuf[100];
	struct mg_context *ctx;
	struct mg_connection *client_conn;
	const struct mg_request_info *ri;
	char uri[64];
	char buf[1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10 + 8];
	const char *expected =
	    "112123123412345123456123456712345678123456789123456789A";
	int i;
	const char *request = "GET /U7 HTTP/1.0\r\n\r\n";
#if defined(USE_IPV6) && defined(NO_SSL)
	const char *HTTP_PORT = "8084,[::]:8086";
	short ipv4_port = 8084;
	short ipv6_port = 8086;
#elif !defined(USE_IPV6) && defined(NO_SSL)
	const char *HTTP_PORT = "8084";
	short ipv4_port = 8084;
#elif defined(USE_IPV6) && !defined(NO_SSL)
	const char *HTTP_PORT = "8084,[::]:8086,8194r,[::]:8196r,8094s,[::]:8096s";
	short ipv4_port = 8084;
	short ipv4s_port = 8094;
	short ipv4r_port = 8194;
	short ipv6_port = 8086;
	short ipv6s_port = 8096;
	short ipv6r_port = 8196;
#elif !defined(USE_IPV6) && !defined(NO_SSL)
	const char *HTTP_PORT = "8084,8194r,8094s";
	short ipv4_port = 8084;
	short ipv4s_port = 8094;
	short ipv4r_port = 8194;
#endif

	const char *OPTIONS[8]; /* initializer list here is rejected by CI test */
	const char *opt;
	FILE *f;
	const char *plain_file_content;
	const char *encoded_file_content;
	int opt_idx = 0;

#if !defined(NO_SSL)
	const char *ssl_cert = locate_ssl_cert();
#endif

#if defined(USE_WEBSOCKET)
	struct tclient_data ws_client1_data = {NULL, 0, 0};
	struct tclient_data ws_client2_data = {NULL, 0, 0};
	struct tclient_data ws_client3_data = {NULL, 0, 0};
	struct mg_connection *ws_client1_conn = NULL;
	struct mg_connection *ws_client2_conn = NULL;
	struct mg_connection *ws_client3_conn = NULL;
#endif

	memset((void *)OPTIONS, 0, sizeof(OPTIONS));
	OPTIONS[opt_idx++] = "listening_ports";
	OPTIONS[opt_idx++] = HTTP_PORT;
#if !defined(NO_FILES)
	OPTIONS[opt_idx++] = "document_root";
	OPTIONS[opt_idx++] = ".";
#endif
#ifndef NO_SSL
	ck_assert(ssl_cert != NULL);
	OPTIONS[opt_idx++] = "ssl_certificate";
	OPTIONS[opt_idx++] = ssl_cert;
#endif
	ck_assert_int_le(opt_idx, (int)(sizeof(OPTIONS) / sizeof(OPTIONS[0])));
	ck_assert(OPTIONS[sizeof(OPTIONS) / sizeof(OPTIONS[0]) - 1] == NULL);
	ck_assert(OPTIONS[sizeof(OPTIONS) / sizeof(OPTIONS[0]) - 2] == NULL);

	ctx = mg_start(NULL, &g_ctx, OPTIONS);
	ck_assert(ctx != NULL);
	g_ctx = ctx;

	opt = mg_get_option(ctx, "listening_ports");
	ck_assert_str_eq(opt, HTTP_PORT);
	opt = mg_get_option(ctx, "cgi_environment");
	ck_assert_str_eq(opt, "");
	opt = mg_get_option(ctx, "unknown_option_name");
	ck_assert(opt == NULL);

	for (i = 0; i < 1000; i++) {
		sprintf(uri, "/U%u", i);
		mg_set_request_handler(ctx, uri, request_test_handler, NULL);
	}
	for (i = 500; i < 800; i++) {
		sprintf(uri, "/U%u", i);
		mg_set_request_handler(ctx, uri, NULL, (void *)1);
	}
	for (i = 600; i >= 0; i--) {
		sprintf(uri, "/U%u", i);
		mg_set_request_handler(ctx, uri, NULL, (void *)2);
	}
	for (i = 750; i <= 1000; i++) {
		sprintf(uri, "/U%u", i);
		mg_set_request_handler(ctx, uri, NULL, (void *)3);
	}
	for (i = 5; i < 9; i++) {
		sprintf(uri, "/U%u", i);
		mg_set_request_handler(ctx,
		                       uri,
		                       request_test_handler,
		                       (void *)(ptrdiff_t)i);
	}

#ifdef USE_WEBSOCKET
	mg_set_websocket_handler(ctx,
	                         "/websocket",
	                         websock_server_connect,
	                         websock_server_ready,
	                         websock_server_data,
	                         websock_server_close,
	                         (void *)7531);
#endif

	/* Try to load non existing file */
	client_conn = mg_download("localhost",
	                          ipv4_port,
	                          0,
	                          ebuf,
	                          sizeof(ebuf),
	                          "%s",
	                          "GET /file/not/found HTTP/1.0\r\n\r\n");
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);
	ck_assert_str_eq(ri->uri, "404");
	mg_close_connection(client_conn);

	/* Get data from callback */
	client_conn = mg_download(
	    "localhost", ipv4_port, 0, ebuf, sizeof(ebuf), "%s", request);
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);
	ck_assert_str_eq(ri->uri, "200");
	i = mg_read(client_conn, buf, sizeof(buf));
	ck_assert_int_eq(i, (int)strlen(expected));
	buf[i] = 0;
	ck_assert_str_eq(buf, expected);
	mg_close_connection(client_conn);

	/* Get data from callback using http://127.0.0.1 */
	client_conn = mg_download(
	    "127.0.0.1", ipv4_port, 0, ebuf, sizeof(ebuf), "%s", request);
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);
	ck_assert_str_eq(ri->uri, "200");
	i = mg_read(client_conn, buf, sizeof(buf));
	ck_assert_int_eq(i, (int)strlen(expected));
	buf[i] = 0;
	ck_assert_str_eq(buf, expected);
	mg_close_connection(client_conn);

#if defined(USE_IPV6)
	/* Get data from callback using http://[::1] */
	client_conn =
	    mg_download("[::1]", ipv6_port, 0, ebuf, sizeof(ebuf), "%s", request);
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);
	ck_assert_str_eq(ri->uri, "200");
	i = mg_read(client_conn, buf, sizeof(buf));
	ck_assert_int_eq(i, (int)strlen(expected));
	buf[i] = 0;
	ck_assert_str_eq(buf, expected);
	mg_close_connection(client_conn);
#endif

#if !defined(NO_SSL)
	/* Get data from callback using https://127.0.0.1 */
	client_conn = mg_download(
	    "127.0.0.1", ipv4s_port, 1, ebuf, sizeof(ebuf), "%s", request);
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);
	ck_assert_str_eq(ri->uri, "200");
	i = mg_read(client_conn, buf, sizeof(buf));
	ck_assert_int_eq(i, (int)strlen(expected));
	buf[i] = 0;
	ck_assert_str_eq(buf, expected);
	mg_close_connection(client_conn);

	/* Get redirect from callback using http://127.0.0.1 */
	client_conn = mg_download(
	    "127.0.0.1", ipv4r_port, 0, ebuf, sizeof(ebuf), "%s", request);
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);
	ck_assert_str_eq(ri->uri, "302");
	i = mg_read(client_conn, buf, sizeof(buf));
	ck_assert_int_eq(i, -1);
	mg_close_connection(client_conn);
#endif

#if defined(USE_IPV6) && !defined(NO_SSL)
	/* Get data from callback using https://[::1] */
	client_conn =
	    mg_download("[::1]", ipv6s_port, 1, ebuf, sizeof(ebuf), "%s", request);
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);
	ck_assert_str_eq(ri->uri, "200");
	i = mg_read(client_conn, buf, sizeof(buf));
	ck_assert_int_eq(i, (int)strlen(expected));
	buf[i] = 0;
	ck_assert_str_eq(buf, expected);
	mg_close_connection(client_conn);

	/* Get redirect from callback using http://127.0.0.1 */
	client_conn =
	    mg_download("[::1]", ipv6r_port, 0, ebuf, sizeof(ebuf), "%s", request);
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);
	ck_assert_str_eq(ri->uri, "302");
	i = mg_read(client_conn, buf, sizeof(buf));
	ck_assert_int_eq(i, -1);
	mg_close_connection(client_conn);
#endif

/* It seems to be impossible to find out what the actual working
 * directory of the CI test environment is. Before breaking another
 * dozen of builds by trying blindly with different paths, just
 * create the file here */
#ifdef _WIN32
	f = fopen("test.txt", "wb");
#else
	f = fopen("test.txt", "w");
#endif
	plain_file_content = "simple text file\n";
	fwrite(plain_file_content, 17, 1, f);
	fclose(f);

#ifdef _WIN32
	f = fopen("test_gz.txt.gz", "wb");
#else
	f = fopen("test_gz.txt.gz", "w");
#endif
	encoded_file_content = "\x1f\x8b\x08\x08\xf8\x9d\xcb\x55\x00\x00"
	                       "test_gz.txt"
	                       "\x00\x01\x11\x00\xee\xff"
	                       "zipped text file"
	                       "\x0a\x34\x5f\xcc\x49\x11\x00\x00\x00";
	fwrite(encoded_file_content, 1, 52, f);
	fclose(f);

	/* Get static data */
	client_conn = mg_download("localhost",
	                          ipv4_port,
	                          0,
	                          ebuf,
	                          sizeof(ebuf),
	                          "%s",
	                          "GET /test.txt HTTP/1.0\r\n\r\n");
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);

#if defined(NO_FILES)
	ck_assert_str_eq(ri->uri, "404");
#else
	ck_assert_str_eq(ri->uri, "200");
	i = mg_read(client_conn, buf, sizeof(buf));
	ck_assert_int_eq(i, 17);
	if ((i >= 0) && (i < (int)sizeof(buf))) {
		buf[i] = 0;
	}
	ck_assert_str_eq(buf, plain_file_content);
#endif
	mg_close_connection(client_conn);

	/* Get zipped static data - will not work if Accept-Encoding is not set */
	client_conn = mg_download("localhost",
	                          ipv4_port,
	                          0,
	                          ebuf,
	                          sizeof(ebuf),
	                          "%s",
	                          "GET /test_gz.txt HTTP/1.0\r\n\r\n");
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);

	ck_assert_str_eq(ri->uri, "404");
	mg_close_connection(client_conn);

	/* Get zipped static data - with Accept-Encoding */
	client_conn = mg_download(
	    "localhost",
	    ipv4_port,
	    0,
	    ebuf,
	    sizeof(ebuf),
	    "%s",
	    "GET /test_gz.txt HTTP/1.0\r\nAccept-Encoding: gzip\r\n\r\n");
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);

#if defined(NO_FILES)
	ck_assert_str_eq(ri->uri, "404");
#else
	ck_assert_str_eq(ri->uri, "200");
	i = mg_read(client_conn, buf, sizeof(buf));
	ck_assert_int_eq(i, 52);
	if ((i >= 0) && (i < (int)sizeof(buf))) {
		buf[i] = 0;
	}
	ck_assert_int_eq(ri->content_length, 52);
	ck_assert_str_eq(buf, encoded_file_content);
#endif
	mg_close_connection(client_conn);

	/* Get directory listing */
	client_conn = mg_download("localhost",
	                          ipv4_port,
	                          0,
	                          ebuf,
	                          sizeof(ebuf),
	                          "%s",
	                          "GET / HTTP/1.0\r\n\r\n");
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);
#if defined(NO_FILES)
	ck_assert_str_eq(ri->uri, "404");
#else
	ck_assert_str_eq(ri->uri, "200");
	i = mg_read(client_conn, buf, sizeof(buf));
	ck_assert(i > 6);
	buf[6] = 0;
	ck_assert_str_eq(buf, "<html>");
#endif
	mg_close_connection(client_conn);

	/* POST to static file (will not work) */
	client_conn = mg_download("localhost",
	                          ipv4_port,
	                          0,
	                          ebuf,
	                          sizeof(ebuf),
	                          "%s",
	                          "POST /test.txt HTTP/1.0\r\n\r\n");
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);
#if defined(NO_FILES)
	ck_assert_str_eq(ri->uri, "404");
#else
	ck_assert_str_eq(ri->uri, "405");
	i = mg_read(client_conn, buf, sizeof(buf));
	ck_assert(i >= 29);
	buf[29] = 0;
	ck_assert_str_eq(buf, "Error 405: Method Not Allowed");
#endif
	mg_close_connection(client_conn);

	/* PUT to static file (will not work) */
	client_conn = mg_download("localhost",
	                          ipv4_port,
	                          0,
	                          ebuf,
	                          sizeof(ebuf),
	                          "%s",
	                          "PUT /test.txt HTTP/1.0\r\n\r\n");
	ck_assert(client_conn != NULL);
	ri = mg_get_request_info(client_conn);

	ck_assert(ri != NULL);
#if defined(NO_FILES)
	ck_assert_str_eq(ri->uri, "405"); /* method not allowed */
#else
	ck_assert_str_eq(ri->uri, "401"); /* not authorized */
#endif
	mg_close_connection(client_conn);

/* Websocket test */
#ifdef USE_WEBSOCKET
	/* Then connect a first client */
	ws_client1_conn =
	    mg_connect_websocket_client("localhost",
	                                ipv4_port,
	                                0,
	                                ebuf,
	                                sizeof(ebuf),
	                                "/websocket",
	                                NULL,
	                                websocket_client_data_handler,
	                                websocket_client_close_handler,
	                                &ws_client1_data);

	ck_assert(ws_client1_conn != NULL);

	wait_not_null(
	    &(ws_client1_data.data)); /* Wait for the websocket welcome message */
	ck_assert_int_eq(ws_client1_data.closed, 0);
	ck_assert_int_eq(ws_client2_data.closed, 0);
	ck_assert_int_eq(ws_client3_data.closed, 0);
	ck_assert(ws_client2_data.data == NULL);
	ck_assert_uint_eq(ws_client2_data.len, 0);
	ck_assert(ws_client1_data.data != NULL);
	ck_assert_uint_eq(ws_client1_data.len, websocket_welcome_msg_len);
	ck_assert(!memcmp(ws_client1_data.data,
	                  websocket_welcome_msg,
	                  websocket_welcome_msg_len));
	free(ws_client1_data.data);
	ws_client1_data.data = NULL;
	ws_client1_data.len = 0;

	mg_websocket_client_write(ws_client1_conn,
	                          WEBSOCKET_OPCODE_TEXT,
	                          "data1",
	                          5);

	wait_not_null(
	    &(ws_client1_data
	          .data)); /* Wait for the websocket acknowledge message */
	ck_assert_int_eq(ws_client1_data.closed, 0);
	ck_assert_int_eq(ws_client2_data.closed, 0);
	ck_assert(ws_client2_data.data == NULL);
	ck_assert_uint_eq(ws_client2_data.len, 0);
	ck_assert(ws_client1_data.data != NULL);
	ck_assert_uint_eq(ws_client1_data.len, 3);
	ck_assert(!memcmp(ws_client1_data.data, "ok1", 3));
	free(ws_client1_data.data);
	ws_client1_data.data = NULL;
	ws_client1_data.len = 0;

/* Now connect a second client */
#ifdef USE_IPV6
	ws_client2_conn =
	    mg_connect_websocket_client("[::1]",
	                                ipv6_port,
	                                0,
	                                ebuf,
	                                sizeof(ebuf),
	                                "/websocket",
	                                NULL,
	                                websocket_client_data_handler,
	                                websocket_client_close_handler,
	                                &ws_client2_data);
#else
	ws_client2_conn =
	    mg_connect_websocket_client("127.0.0.1",
	                                ipv4_port,
	                                0,
	                                ebuf,
	                                sizeof(ebuf),
	                                "/websocket",
	                                NULL,
	                                websocket_client_data_handler,
	                                websocket_client_close_handler,
	                                &ws_client2_data);
#endif
	ck_assert(ws_client2_conn != NULL);

	wait_not_null(
	    &(ws_client2_data.data)); /* Wait for the websocket welcome message */
	ck_assert(ws_client1_data.closed == 0);
	ck_assert(ws_client2_data.closed == 0);
	ck_assert(ws_client1_data.data == NULL);
	ck_assert(ws_client1_data.len == 0);
	ck_assert(ws_client2_data.data != NULL);
	ck_assert(ws_client2_data.len == websocket_welcome_msg_len);
	ck_assert(!memcmp(ws_client2_data.data,
	                  websocket_welcome_msg,
	                  websocket_welcome_msg_len));
	free(ws_client2_data.data);
	ws_client2_data.data = NULL;
	ws_client2_data.len = 0;

	mg_websocket_client_write(ws_client1_conn,
	                          WEBSOCKET_OPCODE_TEXT,
	                          "data2",
	                          5);

	wait_not_null(
	    &(ws_client1_data
	          .data)); /* Wait for the websocket acknowledge message */
	ck_assert(ws_client1_data.closed == 0);
	ck_assert(ws_client2_data.closed == 0);
	ck_assert(ws_client2_data.data == NULL);
	ck_assert(ws_client2_data.len == 0);
	ck_assert(ws_client1_data.data != NULL);
	ck_assert(ws_client1_data.len == 4);
	ck_assert(!memcmp(ws_client1_data.data, "ok 2", 4));
	free(ws_client1_data.data);
	ws_client1_data.data = NULL;
	ws_client1_data.len = 0;

	mg_websocket_client_write(ws_client1_conn, WEBSOCKET_OPCODE_TEXT, "bye", 3);

	wait_not_null(
	    &(ws_client1_data.data)); /* Wait for the websocket goodbye message */
	ck_assert(ws_client1_data.closed == 0);
	ck_assert(ws_client2_data.closed == 0);
	ck_assert(ws_client2_data.data == NULL);
	ck_assert(ws_client2_data.len == 0);
	ck_assert(ws_client1_data.data != NULL);
	ck_assert(ws_client1_data.len == websocket_goodbye_msg_len);
	ck_assert(!memcmp(ws_client1_data.data,
	                  websocket_goodbye_msg,
	                  websocket_goodbye_msg_len));
	free(ws_client1_data.data);
	ws_client1_data.data = NULL;
	ws_client1_data.len = 0;

	mg_close_connection(ws_client1_conn);

	test_sleep(3); /* Won't get any message */
	ck_assert(ws_client1_data.closed == 1);
	ck_assert(ws_client2_data.closed == 0);
	ck_assert(ws_client1_data.data == NULL);
	ck_assert(ws_client1_data.len == 0);
	ck_assert(ws_client2_data.data == NULL);
	ck_assert(ws_client2_data.len == 0);

	mg_websocket_client_write(ws_client2_conn, WEBSOCKET_OPCODE_TEXT, "bye", 3);

	wait_not_null(
	    &(ws_client2_data.data)); /* Wait for the websocket goodbye message */
	ck_assert(ws_client1_data.closed == 1);
	ck_assert(ws_client2_data.closed == 0);
	ck_assert(ws_client1_data.data == NULL);
	ck_assert(ws_client1_data.len == 0);
	ck_assert(ws_client2_data.data != NULL);
	ck_assert(ws_client2_data.len == websocket_goodbye_msg_len);
	ck_assert(!memcmp(ws_client2_data.data,
	                  websocket_goodbye_msg,
	                  websocket_goodbye_msg_len));
	free(ws_client2_data.data);
	ws_client2_data.data = NULL;
	ws_client2_data.len = 0;

	mg_close_connection(ws_client2_conn);

	test_sleep(3); /* Won't get any message */
	ck_assert(ws_client1_data.closed == 1);
	ck_assert(ws_client2_data.closed == 1);
	ck_assert(ws_client1_data.data == NULL);
	ck_assert(ws_client1_data.len == 0);
	ck_assert(ws_client2_data.data == NULL);
	ck_assert(ws_client2_data.len == 0);

	/* Connect client 3 */
	ws_client3_conn =
	    mg_connect_websocket_client("localhost",
#if defined(NO_SSL)
	                                ipv4_port,
	                                0,
#else
	                                ipv4s_port,
	                                1,
#endif
	                                ebuf,
	                                sizeof(ebuf),
	                                "/websocket",
	                                NULL,
	                                websocket_client_data_handler,
	                                websocket_client_close_handler,
	                                &ws_client3_data);

	ck_assert(ws_client3_conn != NULL);

	wait_not_null(
	    &(ws_client3_data.data)); /* Wait for the websocket welcome message */
	ck_assert(ws_client1_data.closed == 1);
	ck_assert(ws_client2_data.closed == 1);
	ck_assert(ws_client3_data.closed == 0);
	ck_assert(ws_client1_data.data == NULL);
	ck_assert(ws_client1_data.len == 0);
	ck_assert(ws_client2_data.data == NULL);
	ck_assert(ws_client2_data.len == 0);
	ck_assert(ws_client3_data.data != NULL);
	ck_assert(ws_client3_data.len == websocket_welcome_msg_len);
	ck_assert(!memcmp(ws_client3_data.data,
	                  websocket_welcome_msg,
	                  websocket_welcome_msg_len));
	free(ws_client3_data.data);
	ws_client3_data.data = NULL;
	ws_client3_data.len = 0;
#endif

	/* Close the server */
	g_ctx = NULL;
	mg_stop(ctx);

#ifdef USE_WEBSOCKET
	for (i = 0; i < 100; i++) {
		test_sleep(1);
		if (ws_client3_data.closed != 0) {
			break;
		}
	}

	ck_assert_int_eq(ws_client3_data.closed, 1);
#endif
}
END_TEST


Suite *
make_public_server_suite(void)
{
	Suite *const suite = suite_create("PublicServer");

	TCase *const tcase_checktestenv = tcase_create("Check test environment");
	TCase *const tcase_startthreads = tcase_create("Start threads");
	TCase *const tcase_startstophttp = tcase_create("Start Stop HTTP Server");
	TCase *const tcase_startstophttps = tcase_create("Start Stop HTTPS Server");
	TCase *const tcase_serverandclienttls = tcase_create("TLS Server Client");
	TCase *const tcase_serverrequests = tcase_create("Server Requests");

	tcase_add_test(tcase_checktestenv, test_the_test_environment);
	tcase_set_timeout(tcase_checktestenv, civetweb_min_test_timeout);
	suite_add_tcase(suite, tcase_checktestenv);

	tcase_add_test(tcase_startthreads, test_threading);
	tcase_set_timeout(tcase_startthreads, civetweb_min_test_timeout);
	suite_add_tcase(suite, tcase_startthreads);

	tcase_add_test(tcase_startstophttp, test_mg_start_stop_http_server);
	tcase_set_timeout(tcase_startstophttp, civetweb_min_test_timeout);
	suite_add_tcase(suite, tcase_startstophttp);

	tcase_add_test(tcase_startstophttps, test_mg_start_stop_https_server);
	tcase_set_timeout(tcase_startstophttps, civetweb_min_test_timeout);
	suite_add_tcase(suite, tcase_startstophttps);

	tcase_add_test(tcase_serverandclienttls, test_mg_server_and_client_tls);
	tcase_set_timeout(tcase_serverandclienttls, civetweb_min_test_timeout);
	suite_add_tcase(suite, tcase_serverandclienttls);

	tcase_add_test(tcase_serverrequests, test_request_handlers);
	tcase_set_timeout(tcase_serverrequests, 120);
	suite_add_tcase(suite, tcase_serverrequests);

	return suite;
}

#ifdef REPLACE_CHECK_FOR_LOCAL_DEBUGGING
/* Used to debug test cases without using the check framework */

static int chk_ok = 0;
static int chk_failed = 0;

void
main(void)
{
	test_the_test_environment(0);
	test_threading(0);
	test_mg_start_stop_http_server(0);
	test_mg_start_stop_https_server(0);
	test_request_handlers(0);
	test_mg_server_and_client_tls(0);

	printf("\nok: %i\nfailed: %i\n\n", chk_ok, chk_failed);
}

void
_ck_assert_failed(const char *file, int line, const char *expr, ...)
{
	va_list va;
	va_start(va, expr);
	fprintf(stderr, "Error: %s, line %i\n", file, line); /* breakpoint here ! */
	vfprintf(stderr, expr, va);
	fprintf(stderr, "\n\n");
	va_end(va);
	chk_failed++;
}

void
_ck_assert_msg(int cond, const char *file, int line, const char *expr, ...)
{
	va_list va;

	if (cond) {
		chk_ok++;
		return;
	}

	va_start(va, expr);
	fprintf(stderr, "Error: %s, line %i\n", file, line); /* breakpoint here ! */
	vfprintf(stderr, expr, va);
	fprintf(stderr, "\n\n");
	va_end(va);
	chk_failed++;
}

void
_mark_point(const char *file, int line)
{
	chk_ok++;
}

void
tcase_fn_start(const char *fname, const char *file, int line)
{
}
void suite_add_tcase(Suite *s, TCase *tc){};
void _tcase_add_test(TCase *tc,
                     TFun tf,
                     const char *fname,
                     int _signal,
                     int allowed_exit_value,
                     int start,
                     int end){};
TCase *
tcase_create(const char *name)
{
	return NULL;
};
Suite *
suite_create(const char *name)
{
	return NULL;
};
void tcase_set_timeout(TCase *tc, double timeout){};

#endif
