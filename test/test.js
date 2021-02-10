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

import wilton from "wilton";
const { Channel, fs, Logger, misc, wiltoncall } = wilton;

// setup logging
const wconf = misc.wiltonConfig();
Logger.initialize({
    appenders: [
        {
            appenderType: "FILE",
            thresholdLevel: "DEBUG",
            filePath: "/var/log/nginx/bch_app.log"
        }
    ],
    loggers: {
        "staticlib": "WARN",
        "wilton": "WARN",
        "wilton.nginx": "DEBUG"
    }
});
const logger = new Logger(import.meta.id);

const chan = Channel.lookup("wilton/nginx/requests");

for(;;) {
    const req = chan.receive();
    if (null === req) {
        break;
    }
    //logger.info(JSON.stringify(req, null, 4));
    let resp = null;
    if ("file" === req.data.format) {
        logger.info("Receiving file");
        logger.info(`path: [${req.data.file}]`);
        //const data = fs.readFile(req.data.file);
        //logger.info(`size: [${data.length}]`);
        fs.writeFile("/var/log/nginx/bch.tmp", JSON.stringify(req, null, 4));
        resp = {
            handle: req.handle,
            status: 200,
            headers: {
                "X-Foo-Bar": "Boo",
                "X-Background-Content-Handler-Data-File": "/var/log/nginx/bch.tmp"
            }
        };
    } else {
        resp = {
            handle: req.handle,
            status: 200,
            headers: {
                "X-Foo-Bar": "Baz"
            },
            data: req
        };
    } 
    try {
        wiltoncall("nginx_send_response", resp);
    } catch(e) {
        logger.error(e);
    }
}
logger.info("App shut down");