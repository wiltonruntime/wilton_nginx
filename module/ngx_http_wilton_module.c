/*
 * Copyright 2021, alex at staticlibs.net
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_channel.h>
#include <ngx_http.h>

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else // !WIN32
#include <dlfcn.h>
#include <unistd.h>
#endif // _WIN32

typedef char*(*wiltoncall_fun)(const char*, int, const char*, int, char**, int*);
typedef void*(*wilton_free_fun)(char*);
typedef char*(*wilton_embed_init_fun)(const char*, int, const char*, int, const char*, int);

static char *ngx_http_wilton(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_wilton_handler(ngx_http_request_t *r);
static void pipe_event_handler(ngx_event_t* ev);

typedef struct fds_pair {
    int fds_in;
    int fds_out;
} fds_pair;

// globals
static fds_pair wilton_pipe;
static wiltoncall_fun wiltoncall = NULL;
static wilton_free_fun wilton_free = NULL;
static const char* gateway_module = NULL;

static u_char ngx_wilton[] = "hello world\r\n";
static u_char resp[1024];

ngx_int_t initialize(ngx_cycle_t* cycle) {

    // init wilton

    // todo: get this from config
    const char* whome = "/home/alex/projects/wilton/build/wilton_dist";
    const char* engine = "quickjs";
    const char* app_dir = "/home/alex/projects/wilton_other/wngx";
    gateway_module = "wngx/hi";
    char str[1024];

    // load shared libs
    snprintf(str, sizeof(str), "%s%s", whome, "/bin/libwilton_core.so");
    void* core_lib = dlopen(str , RTLD_LAZY);
    if (NULL == core_lib) {
        fprintf(stderr, "init: 'wilton_core' dlopen failed\n");
        return NGX_ERROR;
    }
    snprintf(str, sizeof(str), "%s%s", whome, "/bin/libwilton_embed.so");
    void* embed_lib = dlopen(str, RTLD_LAZY);
    if (NULL == embed_lib) {
        fprintf(stderr, "init: 'wilton_embed' dlopen failed\n");
        return NGX_ERROR;
    }

    // lookup symbols
    wiltoncall = (wiltoncall_fun) dlsym(core_lib, "wiltoncall");
    if (NULL == wiltoncall) {
        fprintf(stderr, "init: 'wiltoncall' dlsym failed\n");
        return NGX_ERROR;
    }
    wilton_free = (wilton_free_fun) dlsym(core_lib, "wilton_free");
    if (NULL == wilton_free) {
        fprintf(stderr, "init: 'wilton_free' dlsym failed\n");
        return NGX_ERROR;
    }
    wilton_embed_init_fun embed_init = (wilton_embed_init_fun) dlsym(embed_lib, "wilton_embed_init");
    if (NULL == embed_init) {
        fprintf(stderr, "init: 'wilton_embed_init' dlsym failed\n");
        return NGX_ERROR;
    }

    // call init
    char* err_init = embed_init(whome, (int)(strlen(whome)),
            engine, (int)(strlen(engine)), app_dir, (int)(strlen(app_dir)));
    if (NULL != err_init) {
        fprintf(stderr, "init: 'wilton_embed_init' failed, message: [%s]\n", err_init);
        wilton_free(err_init);
        return NGX_ERROR;
    }

    // init pipe

    // create pipe
    int fds[2];
    int err = pipe(fds);
    if (0 != err) {
        fprintf(stderr, "init: 'pipe' failed\n");
        return NGX_ERROR;
    }

    // make descriptors non-blocking
    int err_fds1_nb = ngx_nonblocking(fds[0]);
    if (-1 == err_fds1_nb) {
        fprintf(stderr, "init: 'ngx_nonblocking' for fds1 failed, code: [%d]\n", err_fds1_nb);
        return NGX_ERROR;
    }
    int err_fds2_nb = ngx_nonblocking(fds[1]);
    if (-1 == err_fds2_nb) {
        fprintf(stderr, "init: 'ngx_nonblocking' for fds1 failed, code [%d]\n", err_fds2_nb);
        return NGX_ERROR;
    }

    // register listener on a pipe
    ngx_int_t rc = ngx_add_channel_event(cycle, fds[0], NGX_READ_EVENT, pipe_event_handler);
    if (NGX_OK != rc) {
        fprintf(stderr, "init: 'ngx_add_channel_event' failed, code: [%ld]\n", rc);
        return NGX_ERROR;
    }

    // expose pipe on global var
    wilton_pipe.fds_in = fds[0];
    wilton_pipe.fds_in = fds[1];

    return NGX_OK;
}

/**
 * This module provided directive: hello world.
 *
 */
static ngx_command_t ngx_http_wilton_commands[] = {

    { ngx_string("wilton"), /* directive */
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS, /* location context and takes
                                            no arguments*/
      ngx_http_wilton, /* configuration setup function */
      0, /* No offset. Only one context is supported. */
      0, /* No offset when storing the module configuration on struct. */
      NULL},

    ngx_null_command /* command termination */
};

/* The module context. */
static ngx_http_module_t ngx_http_wilton_module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */

    NULL, /* create main configuration */
    NULL, /* init main configuration */

    NULL, /* create server configuration */
    NULL, /* merge server configuration */

    NULL, /* create location configuration */
    NULL /* merge location configuration */
};

void pipe_event_handler(ngx_event_t* ev) {
    ngx_connection_t* c = (ngx_connection_t*) ev->data;
    ngx_int_t result = ngx_handle_read_event(ev, 0);
    if (result != NGX_OK) {
        fprintf(stderr, "pipe: ngx_handle_read_event error: %ld\n", result);
    }

    ngx_http_request_t* r;
    ngx_int_t size = read(c->fd, &r, sizeof(r));
    if (size == -1) {
        fprintf(stderr, "pipe: read error\n");
    }

    ngx_buf_t *b;
    ngx_chain_t out;

    /* Set the Content-Type header. */
    r->headers_out.content_type.len = sizeof("text/plain") - 1;
    r->headers_out.content_type.data = (u_char *) "text/plain";

    /* Allocate a new buffer for sending out the reply. */
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));

    /* Insertion in the buffer chain. */
    out.buf = b;
    out.next = NULL; /* just one buffer */
    
    b->pos = ngx_wilton; /* first position in memory of the data */
    b->last = ngx_wilton + sizeof(ngx_wilton) - 1; /* last position in memory of the data */
    b->memory = 1; /* content is in read-only memory */
    b->last_buf = 1; /* there will be no more buffers in the request */

    /* Sending the headers for the reply. */
    r->headers_out.status = NGX_HTTP_OK; /* 200 status code */
    /* Get the content length of the body. */
    r->headers_out.content_length_n = sizeof(ngx_wilton) - 1;
    ngx_http_send_header(r); /* Send the headers */

    /* Send the body, and return the status code of the output filter chain. */
    ngx_http_output_filter(r, &out);
    ngx_http_finalize_request(r, NGX_HTTP_OK);
}

/* Module definition. */
ngx_module_t ngx_http_wilton_module = {
    NGX_MODULE_V1,
    &ngx_http_wilton_module_ctx, /* module context */
    ngx_http_wilton_commands, /* module directives */
    NGX_HTTP_MODULE, /* module type */
    NULL, /* init master */
    NULL, /* init module */
    initialize, /* init process */
    NULL, /* init thread */
    NULL, /* exit thread */
    NULL, /* exit process */
    NULL, /* exit master */
    NGX_MODULE_V1_PADDING
};

/**
 * Content handler.
 *
 * @param r
 *   Pointer to the request structure. See http_request.h.
 * @return
 *   The status of the response generation.
 */
static ngx_int_t ngx_http_wilton_handler(ngx_http_request_t *r)
{
    const char* call_runscript = "runscript_quickjs";
    const char* call_desc_json = "{\"module\": \"wngx/hi\"}";

    char* json_out = NULL;
    int json_len_out = -1;
    char*  err = wiltoncall(call_runscript, (int)(strlen(call_runscript)),
            call_desc_json, (int)(strlen(call_desc_json)),
            &json_out, &json_len_out);


    if (NULL != err) {
        fprintf(stderr, "wilton error: [%s]\n", err);
        wilton_free(err);
        return NGX_ERROR;
    }

    /* Set the Content-Type header. */
    r->headers_out.content_type.len = sizeof("text/plain") - 1;
    r->headers_out.content_type.data = (u_char *) "text/plain";

    /* Allocate a new buffer for sending out the reply. */
    ngx_buf_t* b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));

    /* Insertion in the buffer chain. */
    ngx_chain_t out;
    out.buf = b;
    out.next = NULL; /* just one buffer */

    size_t len = json_len_out < 1024 ? json_len_out : 1024;
    memcpy(resp, json_out, len);
    wilton_free(json_out);

    b->pos = resp; /* first position in memory of the data */
    b->last = resp + len; /* last position in memory of the data */
    b->memory = 1; /* content is in read-only memory */
    b->last_buf = 1; /* there will be no more buffers in the request */

    /* Sending the headers for the reply. */
    r->headers_out.status = NGX_HTTP_OK; /* 200 status code */
    /* Get the content length of the body. */
    r->headers_out.content_length_n = len   ;
    ngx_http_send_header(r); /* Send the headers */

    /* Send the body, and return the status code of the output filter chain. */
    return ngx_http_output_filter(r, &out);
} /* ngx_http_wilton_handler */

/**
 * Configuration setup function that installs the content handler.
 *
 * @param cf
 *   Module configuration structure pointer.
 * @param cmd
 *   Module directives structure pointer.
 * @param conf
 *   Module configuration structure pointer.
 * @return string
 *   Status of the configuration setup.
 */
static char *ngx_http_wilton(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf; /* pointer to core location configuration */

    /* Install the hello world handler. */
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_wilton_handler;

    return NGX_CONF_OK;
} /* ngx_http_wilton */
