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
    "random",
    "wilton/crypto",
    "wilton/fs",
    "wilton/httpClient",
    "wilton/misc",
    "wilton/utils"
], function(assert, Random, crypto, fs, http, misc, utils) {
    "use strict";

    var rand = new Random(Random.engines.mt19937().autoSeed());
    var baseUrl = "http://127.0.0.1:8080/wilton?";

    function doWork(opts) {
        var resp_empty = http.sendRequest(baseUrl + "mirror");
        assert.equal(resp_empty.responseCode, 200);
        assert.equal(resp_empty.data, "");

        var resp_json = http.sendRequest(baseUrl + "mirror", {
            data: {
                foo: 42
            },
            meta: {
                timeoutMillis: 30000
            }
        });
        assert.equal(resp_json.responseCode, 200);
        assert.deepEqual(resp_json.json(), {
            foo: 42
        });

        var resp_string = http.sendRequest(baseUrl + "mirror", {
            data: "foobar",
            meta: {
                timeoutMillis: 30000
            }
        });
        assert.equal(resp_string.responseCode, 200);
        assert.equal(resp_string.data, "foobar");

        if ("file" === opts) {
            var src = misc.wiltonConfig().wiltonExecutable;
            var dest = misc.wiltonConfig().wiltonExecutable + "_" + rand.uuid4();
            var hashSrc = crypto.hashFile({
                filePath: src
            });
            var resp_file = http.sendFile(baseUrl + "mirror", {
                filePath: src,
                meta: {
                    responseDataFilePath: dest,
                    timeoutMillis: 30000
                }
            });
            assert.equal(resp_file.responseCode, 200);
            var hashDest = crypto.hashFile({
                filePath: dest
            });
            assert.equal(hashSrc, hashDest);
            fs.unlink(dest);
        }
    };
    
    return function(count, opts) {
        for (var i = 0; i < count; i++) {
            try {
                if (0 === i % 50) {
                    print(i);
                }
                doWork(opts);
            } catch(e) {
                print(utils.formatError(e));
            }
        }
        print("worker exit, count: [" + i + "]");
    };
});
