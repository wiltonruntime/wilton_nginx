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
    "wilton/Logger",
    "wilton/thread"
], function(Logger, thread) {
    "use strict";

    return function(conf) {
        Logger.initConsole("INFO");
        var start = Date.now();
        var channels = [];
        for (var i = 0; i < 12; i++) {
            var ch = thread.run({
                callbackScript: {
                    module: "client/loadWorker",
                    args: [1024*100, "file_"]
                },
                shutdownChannelName: "worker/" + i
            });
            channels.push(ch);
        }
        channels.forEach(function(ch) {
            ch.receiveAndClose();
        });
        var end = Date.now();
        print("Finish success, time: [" + ((end - start)/1000) + "]")
    };
});
