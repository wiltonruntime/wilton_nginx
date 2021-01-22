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

#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <dlfcn.h>
#include <unistd.h>
#else // _WIN32
#include <windows.h>
#define dlsym GetProcAddress
#endif // !_WIN32

typedef char*(*wiltoncall_fun)(const char*, int, const char*, int, char**, int*);
typedef void*(*wilton_free_fun)(char*);
typedef void*(*wilton_dyload_fun)(const char*, int, const char*, int);
typedef char*(*wilton_embed_init_fun)(const char*, int, const char*, int, const char*, int);

// globals
static wiltoncall_fun wiltoncall = NULL;
static wilton_free_fun wilton_free = NULL;
static ngx_str_t wilton_home;
static ngx_str_t wilton_engine;
static ngx_str_t wilton_appdir;
static ngx_str_t wilton_module;

ngx_int_t initialize(ngx_cycle_t* cycle) {

    char str[1024];

    // load shared libs
#ifndef _WIN32
    snprintf(str, sizeof(str), "%s%s", wilton_home.data, "/bin/libwilton_core.so");
    void* core_lib = dlopen(str, RTLD_LAZY);
    snprintf(str, sizeof(str), "%s%s", wilton_home.data, "/bin/libwilton_embed.so");
    void* embed_lib = dlopen(str, RTLD_LAZY);
#else // !_WIN32
    // todo: LoadLibraryW
    snprintf(str, sizeof(str), "%s%s", wilton_home.data, "/bin/wilton_core.dll");
    HANDLE core_lib = LoadLibraryA(str);
    snprintf(str, sizeof(str), "%s%s", wilton_home.data, "/bin/wilton_embed.dll");
    HANDLE embed_lib = LoadLibraryA(str);
#endif // _WIN32
    if (NULL == core_lib) {
        fprintf(stderr, "init: 'wilton_core' dlopen failed\n");
        return NGX_ERROR;
    }
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
    wilton_dyload_fun wilton_dyload = (wilton_dyload_fun) dlsym(core_lib, "wilton_dyload");
    if (NULL == wilton_dyload) {
        fprintf(stderr, "init: 'wilton_dyload' dlsym failed\n");
        return NGX_ERROR;
    }
    wilton_embed_init_fun embed_init = (wilton_embed_init_fun) dlsym(embed_lib, "wilton_embed_init");
    if (NULL == embed_init) {
        fprintf(stderr, "init: 'wilton_embed_init' dlsym failed\n");
        return NGX_ERROR;
    }

    // call init
    char* err_init = embed_init((const char*) wilton_home.data, wilton_home.len,
            (const char*) wilton_engine.data, wilton_engine.len, (const char*) wilton_appdir.data, wilton_appdir.len);
    if (NULL != err_init) {
        fprintf(stderr, "init: 'wilton_embed_init' failed, message: [%s]\n", err_init);
        wilton_free(err_init);
        return NGX_ERROR;
    }

    // todo: better libs loading
    // dyload required libs
    snprintf(str, sizeof(str), "%s%s", wilton_home.data, "/bin/");
    wilton_dyload("wilton_mustache", (int) sizeof("wilton_mustache") - 1, str, (int) strlen(str));
    wilton_dyload("wilton_channel", (int) sizeof("wilton_channel") - 1, str, (int) strlen(str));
    wilton_dyload("wilton_thread", (int) sizeof("wilton_thread") - 1, str, (int) strlen(str));
    wilton_dyload("wilton_http", (int) sizeof("wilton_http") - 1, str, (int) strlen(str));

    fprintf(stderr, "gateway: initialization complete \n");

    return NGX_OK;
}

static ngx_int_t request_handler(ngx_http_request_t *r) {
    const char* call_runscript = "runscript_quickjs";
    char call_desc_json[1024];
    memset(call_desc_json, ' ', sizeof(call_desc_json));
    snprintf(call_desc_json, sizeof(call_desc_json),
            "{\"module\": \"%s\", \"args\": [%lld]}", wilton_module.data, (long long) r);

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

    if (NULL != json_out) {
        wilton_free(json_out);
    }

    // todo: body
    ngx_http_discard_request_body(r);

    r->main->count++;
    return NGX_DONE;

}

static char* conf_wilton(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    /* Install the handler. */
    ngx_http_core_loc_conf_t* clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = request_handler;
    return NGX_CONF_OK;
}

static char* conf_wilton_home(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_str_t* value = cf->args->elts;
    wilton_home = *(value + 1);
    return NGX_CONF_OK;
}

static char* conf_wilton_engine(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_str_t* value = cf->args->elts;
    wilton_engine = *(value + 1);
    return NGX_CONF_OK;
}

static char* conf_wilton_appdir(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_str_t* value = cf->args->elts;
    wilton_appdir = *(value + 1);
    return NGX_CONF_OK;
}

static char* conf_wilton_module(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_str_t* value = cf->args->elts;
    wilton_module = *(value + 1);
    return NGX_CONF_OK;
}

static ngx_command_t conf_desc[] = {

    { ngx_string("wilton_gateway"), /* directive */
      NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS, /* location context and takes
                                            no arguments*/
      conf_wilton, /* configuration setup function */
      NGX_HTTP_LOC_CONF_OFFSET, /* No offset. Only one context is supported. */
      0, /* No offset when storing the module configuration on struct. */
      NULL},

    { ngx_string("wilton_home"),
      NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      conf_wilton_home,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL},

    { ngx_string("wilton_engine"),
      NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      conf_wilton_engine,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL},

    { ngx_string("wilton_appdir"),
      NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      conf_wilton_appdir,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL},

    { ngx_string("wilton_module"),
      NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      conf_wilton_module,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL},

    ngx_null_command /* command termination */
};

static ngx_http_module_t module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */

    NULL, /* create main configuration */
    NULL, /* init main configuration */

    NULL, /* create server configuration */
    NULL, /* merge server configuration */

    NULL, /* create location configuration */
    NULL /* merge location configuration */
};

ngx_module_t ngx_http_wilton_gateway_module = {
    NGX_MODULE_V1,
    &module_ctx, /* module context */
    conf_desc, /* module directives */
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