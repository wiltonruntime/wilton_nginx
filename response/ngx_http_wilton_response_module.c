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
#include <ngx_http.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>


static char *ngx_http_wilton_response(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_wilton_response_handler(ngx_http_request_t *r);

// globals
//static u_char resp[1024];
static u_char ngx_wilton[] = "gateway async resp 3\n";

static ngx_command_t ngx_http_wilton_response_commands[] = {

    { ngx_string("wilton_response"), /* directive */
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS, /* location context and takes
                                            no arguments*/
      ngx_http_wilton_response, /* configuration setup function */
      0, /* No offset. Only one context is supported. */
      0, /* No offset when storing the module configuration on struct. */
      NULL},

    ngx_null_command /* command termination */
};

static ngx_http_module_t ngx_http_wilton_response_module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */

    NULL, /* create main configuration */
    NULL, /* init main configuration */

    NULL, /* create server configuration */
    NULL, /* merge server configuration */

    NULL, /* create location configuration */
    NULL /* merge location configuration */
};

ngx_module_t ngx_http_wilton_response_module = {
    NGX_MODULE_V1,
    &ngx_http_wilton_response_module_ctx, /* module context */
    ngx_http_wilton_response_commands, /* module directives */
    NGX_HTTP_MODULE, /* module type */
    NULL, /* init master */
    NULL, /* init module */
    NULL, /* init process */
    NULL, /* init thread */
    NULL, /* exit thread */
    NULL, /* exit process */
    NULL, /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_http_request_t* find_gateway_request(ngx_http_headers_in_t* headers_in) {
    ngx_list_part_t* part = &headers_in->headers.part;
    ngx_table_elt_t* elts = part->elts;

    for (size_t i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            elts = part->elts;
            i = 0;
        }

        if (0 == strcmp("X-Gateway-Response-Handle", (const char*) elts[i].key.data)) {
            const char* cstr = (const char*) elts[i].value.data;
            char* endptr;
            errno = 0;
            long long handle = strtoll(cstr, &endptr, 0);
            if (errno == ERANGE || cstr + elts[i].value.len != endptr) {
                fprintf(stderr, "Cannot parse handle from string: [%s]\n", cstr);
                return NULL;
            }
            fprintf(stderr, "response: handle found: [%lld]\n", handle);
            return (ngx_http_request_t*) handle;
        }
    }
    return NULL;
}

static ngx_int_t ngx_http_wilton_response_handler(ngx_http_request_t *r) {

    ngx_int_t status = NGX_HTTP_OK;

    // gateway response
    ngx_http_request_t* gr = find_gateway_request(&r->headers_in);
    if (NULL != gr) {

        /* Set the Content-Type header. */
        //gr->headers_out.content_type.len = r->headers_in.content_type->value.len;
        //gr->headers_out.content_type.data = r->headers_in.content_type->value.data;

        /* Allocate a new buffer for sending out the reply. */
        ngx_buf_t* b = ngx_pcalloc(gr->pool, sizeof(ngx_buf_t));

        ngx_chain_t out;
        /* Insertion in the buffer chain. */
        out.buf = b;
        out.next = NULL; /* just one buffer */

        b->pos = ngx_wilton; /* first position in memory of the data */
        b->last = ngx_wilton + sizeof(ngx_wilton) - 1; /* last position in memory of the data */
        b->memory = 1; /* content is in read-only memory */
        b->last_buf = 1; /* there will be no more buffers in the request */

        /* Sending the headers for the reply. */
        gr->headers_out.status = NGX_HTTP_OK; /* 200 status code */
        /* Get the content length of the body. */
        gr->headers_out.content_length_n = sizeof(ngx_wilton) - 1;
        ngx_http_send_header(gr); /* Send the headers */

        /* Send the body, and return the status code of the output filter chain. */
        ngx_http_output_filter(gr, &out);
        ngx_http_finalize_request(gr, NGX_HTTP_OK);
    } else {
        status = NGX_HTTP_BAD_REQUEST;
    }

    // own response

    /* Allocate a new buffer for sending out the reply. */
    ngx_buf_t* b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));

    /* Insertion in the buffer chain. */
    ngx_chain_t out;
    out.buf = b;
    out.next = NULL; /* just one buffer */

    b->pos = 0; /* first position in memory of the data */
    b->last = 0; /* last position in memory of the data */
    b->last_buf = 1; /* there will be no more buffers in the request */

    /* Sending the headers for the reply. */
    r->headers_out.status = status; /* 200 status code */
    /* Get the content length of the body. */
    r->headers_out.content_length_n = 0;
    ngx_http_send_header(r); /* Send the headers */

    /* Send the body, and return the status code of the output filter chain. */
    return ngx_http_output_filter(r, &out);
} /* ngx_http_wilton_response_handler */

static char *ngx_http_wilton_response(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_core_loc_conf_t *clcf; /* pointer to core location configuration */

    /* Install the hello world handler. */
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_wilton_response_handler;

    return NGX_CONF_OK;
} /* ngx_http_wilton_response */

