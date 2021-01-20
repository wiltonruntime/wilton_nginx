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

#include <dlfcn.h>
#include <unistd.h>

static char *ngx_http_wilton(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_wilton_handler(ngx_http_request_t *r);

typedef struct fds_pair {
    int fds_in;
    int fds_out;
} fds_pair;

static fds_pair wilton_pipe;

static u_char ngx_wilton[] = "hello world\r\n";

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

ngx_int_t init_pipe(ngx_cycle_t* cycle) {
    int fds[2];
    int err = pipe(fds);
    if (0 != err) {
        fprintf(stderr, "pipe: init failed\n");
    }
    // todo: fixme
    ngx_nonblocking(fds[0]);
    ngx_nonblocking(fds[1]);
    wilton_pipe.fds_in = fds[0];
    wilton_pipe.fds_in = fds[1];

    ngx_int_t rc = ngx_add_channel_event(cycle, fds[0], NGX_READ_EVENT, pipe_event_handler);
    if (NGX_OK != rc) {
        fprintf(stderr, "pipe: init error\n");
    }
    return NGX_OK;
}

/* Module definition. */
ngx_module_t ngx_http_wilton_module = {
    NGX_MODULE_V1,
    &ngx_http_wilton_module_ctx, /* module context */
    ngx_http_wilton_commands, /* module directives */
    NGX_HTTP_MODULE, /* module type */
    NULL, /* init master */
    NULL, /* init module */
    init_pipe, /* init process */
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
    /*
    auto th = std::thread([r] {
        std::cerr << "spawned_thread" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));
        auto fd = static_pipe().second;
        auto written = write(fd, std::addressof(r), sizeof(r));
        std::cerr << "written" << std::endl;
    });
    th.detach();

    r->main->count++;
    return NGX_DONE;
     */
 
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
