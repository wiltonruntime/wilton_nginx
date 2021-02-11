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

define([
    "module",
    "random",
    "wilton/Channel",
    "wilton/fs",
    "wilton/Logger",
    "wilton/wiltoncall"
], function(module, Random, Channel, fs, Logger, wiltoncall) {
    "use strict";
    var logger = new Logger(module.id);

    const rand = new Random(Random.engines.mt19937().autoSeed());
    var queue = Channel.lookup("wilton/nginx/requests");

    function mirror(conf, req) {
        if ("none" === req.data.format) {
            return null;
        } else if ("json" === req.data.format) {
            return req.data.json;
        } else if ("string" === req.data.format) {
            return req.data.string;
        } else if ("binary" === req.data.format) {
            // todo
        } else if ("file" === req.data.format) {
            var path = conf.nginx.responseBodyTempDir + "/" + rand.uuid4();
            fs.copyFile(req.data.file, path);
            return path;
        }
        throw new Error("Invalid unknown data format, value: [" + req.data.format + "]");
    }

    return function(conf) {
        Logger.initialize(conf.logging);

        for(;;) {
            var req = queue.receive();
            if (null === req) {
                break;
            }
            logger.info("Request received, path: [" + req.meta.uri + "]," +
                    " args: [" + req.meta.args + "], data: [" + JSON.stringify(req, null, 4) + "]");

            var data = null;
            var status = 200;
            var headers = {
                "X-Foo-Bar": "Baz"
            };
            try {
                if ("mirror" === req.meta.args) {
                    data = mirror(conf, req);
                }
                if ("file" === req.data.format) {
                    headers["X-Background-Content-Handler-Data-File"] = data;
                    data = null;
                }
            } catch(e) {
                data = e.message;
                status = 500;
            }
            logger.info("Sending response, headers: [" + JSON.stringify(headers, null, 4) + "]," +
                    " data: [" + data + "]");

            wiltoncall("nginx_send_response", {
                handle: req.handle,
                status: status,
                headers: headers,
                data: data
            });
        }
        logger.info("App shut down");
    };
});