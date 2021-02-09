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

/* 
 * File:   wilton_nginx.cpp
 * Author: alex
 *
 * Created on February 8, 2021, 8:29 PM
 */

#include <cctype>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "utf8.h"

#include "staticlib/io.hpp"
#include "staticlib/json.hpp"
#include "staticlib/ranges.hpp"
#include "staticlib/support.hpp"
#include "staticlib/utils.hpp"
#include "staticlib/tinydir.hpp"

#include "wilton/wilton.h"
#include "wilton/wiltoncall.h"
#include "wilton/wilton_channel.h"
#include "wilton/wilton_embed.h"

#include "wilton/support/buffer.hpp"
#include "wilton/support/exception.hpp"
#include "wilton/support/logging.hpp"
#include "wilton/support/registrar.hpp"

typedef int (*bch_send_response_type)(
        void* request,
        int http_status,
        const char* headers, int headers_len,
        const char* data, int data_len);

namespace { // anonymous

const std::string logger = std::string("wilton.nginx");

bch_send_response_type send_response_fun = nullptr;

wilton_Channel* requests_channel = nullptr;

} // namespace

namespace wilton {
namespace nginx {

sl::json::value read_config(const std::string& conf_path) {
    auto src = sl::tinydir::file_source(conf_path);
    return sl::json::load(src);
}

void call_init(const sl::json::value& conf) {
    auto whome = conf["nginx"]["wiltonHome"].as_string_nonempty_or_throw("nginx.wiltonHome");
    auto engine = conf["nginx"]["engine"].as_string_nonempty_or_throw("nginx.engine");
    auto appdir = conf["nginx"]["appdir"].as_string_nonempty_or_throw("nginx.appdir");
    auto err = wilton_embed_init(whome.data(), static_cast<int>(whome.length()),
            engine.data(), static_cast<int>(engine.length()),
            appdir.data(), static_cast<int>(appdir.length()));
    if (nullptr != err) {
        wilton::support::throw_wilton_error(err, TRACEMSG(err));
    }
}

void dyload_single_module(const std::string& libname, const std::string& bindir) {
    auto err_dyload = wilton_dyload(libname.c_str(), static_cast<int>(libname.length()),
            bindir.c_str(), static_cast<int>(bindir.length()));
    if (nullptr != err_dyload) {
        wilton::support::throw_wilton_error(err_dyload, TRACEMSG(err_dyload));
    }
}

void dyload_modules(const sl::json::value& conf) {
    auto whome = conf["nginx"]["wiltonHome"].as_string_nonempty_or_throw("nginx.wiltonHome");
    auto bindir = whome + "/bin"; 
    auto& mods = conf["nginx"]["modules"].as_array_or_throw("nginx.modules");
    for (const auto& val : mods) {
        auto& modname = val.as_string_nonempty_or_throw("nginx.modules[]");
        dyload_single_module(modname, bindir);
    }
}

wilton_Channel* create_requests_channel(const sl::json::value& conf) {

    // need to use JS API because channel is going to be accessed from JS
    uint16_t size = conf["nginx"]["requestsQueueSize"].as_uint16_or_throw("nginx.requestsQueueSize");
    auto name = std::string("channel_create");
    auto desc = sl::json::dumps({
        { "name", "wilton/nginx/requests" },
        { "size", size }
    });
    char* out;
    int out_len;
    auto err_call = wiltoncall(name.c_str(), static_cast<int>(name.length()),
            desc.c_str(), static_cast<int>(desc.length()),
            std::addressof(out), std::addressof(out_len));
    if (nullptr != err_call) {
        wilton::support::throw_wilton_error(err_call, TRACEMSG(err_call));
    }
    auto out_json = sl::json::load(sl::io::make_span(out, out_len));
    auto ha = out_json["channelHandle"].as_int64_or_throw("channelHandle");
    return reinterpret_cast<wilton_Channel*>(ha);
}

// todo: binary
// todo: file
support::buffer invoke_response_callback(sl::io::span<const char> data) {
    // json parse
    auto json = sl::json::load(data);
    int64_t handle = -1;
    uint16_t status = 0;
    auto headers = std::string("{}");
    auto jdata = std::string();
    auto rdata = std::ref(sl::utils::empty_string());
    for (const sl::json::field& fi : json.as_object()) {
        auto& name = fi.name();
        if ("handle" == name) {
            handle = fi.as_int64_or_throw(name);
        } else if ("status" == name) {
            status = fi.as_uint16_or_throw(name);
        } else if ("headers" == name) {
            headers = fi.val().dumps();
        } else if ("data" == name) {
            if (sl::json::type::object == fi.val().json_type() ||
                    sl::json::type::array == fi.val().json_type()) {
                jdata = fi.val().dumps();
            } else {
                rdata = fi.as_string_nonempty_or_throw(name);
            }
        } else {
            throw support::exception(TRACEMSG("Unknown data field: [" + name + "]"));
        }
    }
    if (-1 == handle) throw support::exception(TRACEMSG(
            "Required parameter 'handle' not specified"));
    if (0 == status) throw support::exception(TRACEMSG(
            "Required parameter 'status' not specified"));
    const std::string& dt = jdata.length() > 0 ? jdata : rdata.get();
    // call nginx
    auto err = send_response_fun(reinterpret_cast<void*>(handle), status,
            headers.c_str(), static_cast<int> (headers.length()),
            dt.c_str(), static_cast<int> (dt.length()));
    if (200 != err) throw support::exception(TRACEMSG(
            "Error sending response, status: [" + sl::support::to_string(err) + "]"));
    return support::make_null_buffer();
}

void register_response_callback() {
    wilton::support::register_wiltoncall("nginx_send_response", invoke_response_callback);
}

void run_app(const sl::json::value& conf) {
    auto engine = conf["nginx"]["engine"].as_string_nonempty_or_throw("nginx.engine");
    auto name = "runscript_" + engine;
    auto call = conf["nginx"]["startup"].dumps();
    char* out = nullptr;
    int out_len = -1;
    auto err = wiltoncall(name.c_str(), static_cast<int>(name.length()),
            call.c_str(), static_cast<int>(call.length()),
            std::addressof(out), std::addressof(out_len));
    if (nullptr != err) {
        wilton::support::throw_wilton_error(err, TRACEMSG(err));
    }
    if (nullptr != out) {
        wilton_free(out);
    }
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.length() != b.length()) {
        return false;
    }
    for (size_t i = 0; i < a.length(); i++) {
        if (tolower(a[i]) != tolower(b[i])) {
            return false;
        }
    }
    return true;
}

bool json_or_unspecified(const sl::json::value& meta) {
    auto& headers = meta["headers"].as_object_or_throw("headers");
    const auto ct = "content-type";
    const auto tjson = "text/json";
    const auto ajson = "application/json";
    for (const auto& fi : headers) {
        if (iequals(ct, fi.name())) {
            const auto& val = fi.val().as_string();
            if (iequals(tjson, val) || iequals(ajson, val)) {
                return true;
            } else {
                return false;
            }
        }
    }
    return true;
}

sl::json::value data_to_json(const char* data, int data_len) {
    try {
        return sl::json::load({data, data_len});
    } catch(const std::exception& e) {
        return sl::json::value();
    }
}

// todo: file
sl::json::value create_req(void* request, const char* metadata, int metadata_len,
        const char* data, int data_len) {
    auto handle = reinterpret_cast<int64_t>(request);
    auto meta = sl::json::load({metadata, metadata_len});
    auto dt = sl::json::value();
    if (nullptr != data) {
        auto ulen = static_cast<uint16_t>(data_len);
        if (utf8::is_valid(data, data + ulen)) {
            auto payload = sl::json::value();
            if (json_or_unspecified(meta)) {
                payload = data_to_json(data, data_len);
            } 
            if (sl::json::type::object == payload.json_type() ||
                    sl::json::type::array == payload.json_type()) {
                dt = sl::json::value({
                    { "format", "json" },
                    { "json", std::move(payload) },
                    { "string", nullptr },
                    { "binary", nullptr }
                });
            } else {
                auto data_st = std::string(data, ulen);
                dt = sl::json::value({
                    { "format", "string" },
                    { "json", nullptr },
                    { "string", std::move(data_st) },
                    { "binary", nullptr }
                });
            }
        } else {
            auto src = sl::io::array_source(data, ulen);
            auto dest = sl::io::string_sink();
            auto sink = sl::io::make_hex_sink(dest);
            sl::io::copy_all(src, sink);
            dt = sl::json::value({
                { "format", "binary" },
                { "json", nullptr },
                { "string", nullptr },
                { "binary", std::move(dest.get_string()) }
            });
        }
    } else {
        dt = sl::json::value({
            { "format", "string" },
            { "json", nullptr },
            { "string", "" },
            { "binary", nullptr }
        });
    }
    return sl::json::value({
        { "handle", handle },
        { "meta", std::move(meta) },
        { "data", std::move(dt) },
    });
}

} // namespace
}

extern "C" int bch_initialize(bch_send_response_type response_callback,
        const char* hanler_config, int hanler_config_len) /* noexcept */ {
    if (nullptr == response_callback || 
            nullptr == hanler_config ||
            !sl::support::is_uint16_positive(hanler_config_len)) return -1;
    try {
        send_response_fun = response_callback;
        auto conf_path = std::string(hanler_config, static_cast<uint16_t>(hanler_config_len));
        auto conf = wilton::nginx::read_config(conf_path);
        wilton::nginx::call_init(conf);
        wilton::nginx::dyload_modules(conf);
        requests_channel = wilton::nginx::create_requests_channel(conf);
        wilton::nginx::register_response_callback();

        auto conf_ptr = new sl::json::value();
        *conf_ptr = std::move(conf);
        auto th = std::thread([conf_ptr]() {
            try {
                wilton::nginx::run_app(*conf_ptr);
            } catch(const std::exception& e) {
                std::cerr << "Application error, message: [" << e.what() << "]" << std::endl;
            }
            delete conf_ptr;
        });
        th.detach();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Application initialization error, message: [" << e.what() << "]" << std::endl;
        send_response_fun = nullptr;
        requests_channel = nullptr;
        return 1;
    }
}

extern "C" int bch_receive_request(void* request, const char* metadata, int metadata_len,
        const char* data, int data_len) /* noexcept */ {
    if (nullptr == request || 
            nullptr == metadata ||
            !sl::support::is_uint16_positive(metadata_len) ||
            (nullptr != data && !sl::support::is_uint32_positive(data_len))) return -1;
    try {
        auto req = wilton::nginx::create_req(request, metadata, metadata_len, data, data_len);
        auto req_st = sl::json::dumps(req);
        int success = 0;
        auto err = wilton_Channel_offer(requests_channel,
                req_st.c_str(), static_cast<int>(req_st.length()),
                std::addressof(success));
        if (nullptr != err) {
            auto err_st = std::string(err);
            wilton_free(err);
            wilton::support::log_error(logger, std::string() + 
                    "Error enqueuing request, message: [" + err_st + "]");
        }
        return 1 == success ? 0 : 1;
    } catch (const std::exception& e) {
        wilton::support::log_error(logger, std::string() + 
                "Error thrown while enqueuing request, message: [" + e.what() + "]");
        return -1;
    }
}
