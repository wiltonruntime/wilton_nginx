
typedef struct fds_pair {
    int fds_in;
    int fds_out;
} fds_pair;

static fds_pair wilton_pipe;

static void pipe_event_handler(ngx_event_t* ev) {
    ngx_connection_t* c = (ngx_connection_t*) ev->data;
    ngx_int_t result = ngx_handle_read_event(ev, 0);
    if (result != NGX_OK) {
        fprintf(stderr, "pipe: ngx_handle_read_event error: %ld\n", (long int) result);
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

static ngx_int_t create_pipe(ngx_cycle_t* cycle) {
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