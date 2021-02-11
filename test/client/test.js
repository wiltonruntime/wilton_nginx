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
    "assert",
    "wilton/crypto",
    "wilton/fs",
    "wilton/httpClient",
    "wilton/misc"
], function(assert, crypto, fs, http, misc) {
    "use strict";

    var baseUrl = "http://127.0.0.1:8080/wilton?";

    return function(conf) {
        print("test: mirror empty");
        var resp_empty = http.sendRequest(baseUrl + "mirror");
        assert.equal(resp_empty.responseCode, 200);
        assert.equal(resp_empty.data, "");

        print("test: mirror json");
        var resp_json = http.sendRequest(baseUrl + "mirror", {
            data: {
                foo: 42
            }
        });
        assert.equal(resp_json.responseCode, 200);
        assert.deepEqual(resp_json.json(), {
            foo: 42
        });

        print("test: mirror string");
        var resp_string = http.sendRequest(baseUrl + "mirror", {
            data: "foobar"
        });
        assert.equal(resp_string.responseCode, 200);
        assert.equal(resp_string.data, "foobar");

        // todo:
//        print("test: mirror binary");

        print("test: mirror file");
        var src = misc.wiltonConfig().wiltonExecutable;
        var dest = misc.wiltonConfig().wiltonExecutable + "_COPY";
        var hashSrc = crypto.hashFile({
            filePath: src
        });
        var resp_file = http.sendFile(baseUrl + "mirror", {
            filePath: src,
            meta: {
                responseDataFilePath: dest
            }
        });
        assert.equal(resp_file.responseCode, 200);
        var hashDest = crypto.hashFile({
            filePath: dest
        });
        assert.equal(hashSrc, hashDest);
        fs.unlink(dest);
    };
});
