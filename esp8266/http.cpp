/*
 * WiFiStation
 * Copyright (c) 2021 joshua stein <jcs@jcs.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <ESP8266WebServer.h>
#include "wifistation.h"

std::unique_ptr<ESP8266WebServer> http = NULL;

static const char html_wrapper[] PROGMEM = R"END(<!doctype html>
<html>
<head>
	<meta http-equiv="content-type" content="text/html; charset=utf-8" />
	<title>WiFiStation at %s</title>
	<style type="text/css">
		body {
			background-color: #fffff8;
			font-family: sans-serif;
			font-size: 12pt;
			padding: 1em;
		}
		h3 {
			margin: 0;
		}
	</style>
</head>
<body>
<h3>WiFiStation %s</h3>
<p>
%s
</body>
</html>
)END";

static unsigned int upload_size = 0;
static unsigned int delivered_bytes = 0;

void
http_process(void)
{
	if (!settings->http_server)
		return;

	http->handleClient();
}

void
http_send_result(int status, bool include_home, char *body, ...)
{
	size_t len;
	va_list arg;
	const char *accept = http->header("Accept").c_str();
	char *doc = NULL;

	/* expand body format */
	va_start(arg, body);
	len = vasprintf(&doc, body, arg);
	va_end(arg);

	if (len == -1)
		goto fail;

	/* if Accept header starts with text/html, client prefers html */
	if (accept && strncmp(accept, "text/html", 9) == 0) {
		char *tmp;

		/* append home link to body */
		if (include_home) {
			len = asprintf(&tmp, "%s"
			    "<p>"
			    "<a href=\"/\">Home</a>", doc);
			if (len == -1)
				goto fail;
			free(doc);
			doc = tmp;
		}

		/* insert body into html template */
		len = asprintf(&tmp, html_wrapper,
		    WiFi.localIP().toString().c_str(), WIFISTATION_VERSION,
		    doc);
		if (len == -1)
			goto fail;
		free(doc);
		doc = tmp;

		http->send(status, "text/html", doc);
	} else {
		/* append newline since this is probably going to a terminal */
		doc = (char *)realloc(doc, len + 2);
		doc[len] = '\n';
		doc[len + 1] = '\0';
		http->send(status, "text/plain", doc);
	}

	free(doc);
	return;

fail:
	if (doc != NULL)
		free(doc);
	http->send(500, "text/plain", "out of memory :(");
	return;
}

void
http_setup(void)
{
	if (settings->http_server) {
		if (http)
			return;

		http.reset(new ESP8266WebServer(80));
	} else {
		if (http)
			http = NULL;

		return;
	}

	const char *headerkeys[] = { "Accept" };
	http->collectHeaders(headerkeys, 1);

	http->on("/", HTTP_GET, []() {
		char *doc = NULL;

		http_send_result(200, false, R"END(
<form action="/upload" method="POST" enctype="multipart/form-data">
<p>
Binary to execute on MailStation (<em>maximum size is %d bytes</em>):
<p>
<input type="file" name="file" maxlength="%d" size="%d">
<br>
<p>
Ensure the WSLoader application is running on the MailStation ready to accept
the upload.
<p>
<input type="submit" value="Upload">
</form>
)END",
		    MAX_UPLOAD_SIZE, MAX_UPLOAD_SIZE, MAX_UPLOAD_SIZE);
	});

	/*
	 * This measure step is because we don't get the total size of the
	 * upload until we read the whole thing, but we need to send the file
	 * size to the MailStation before sending any data.  So to avoid
	 * caching the entire upload, we process the upload and just count the
	 * bytes being passed through, then send a 307 redirect to the actual
	 * upload endpoint with the total size.  Browsers follow a 307 with a
	 * POST, so at /upload we'll have the actual size before it sends us
	 * the same data.
	 */
	http->on("/upload", HTTP_POST, []() {
		http_send_result(400, true, "Failed receiving file.");
	}, []() {
		HTTPUpload& upload = http->upload();
		char tmp[32];
		int i;

		switch (upload.status) {
		case UPLOAD_FILE_START:
			upload_size = 0;
			break;
		case UPLOAD_FILE_WRITE:
			upload_size += upload.currentSize;
			break;
		case UPLOAD_FILE_END:
			if (upload_size == 0) {
				http_send_result(400, true,
				    "Failed receiving file.");
				return;
			}

			if (upload_size > MAX_UPLOAD_SIZE) {
				http_send_result(400, true,
				    "File upload cannot be larger than %d "
				    "bytes.", MAX_UPLOAD_SIZE);
				return;
			}

			snprintf(tmp, sizeof(tmp), "/upload_measured?size=%d",
			    upload_size);
			http->sendHeader("Location", tmp);
			http->send(307);
			break;
		}
	});

	http->on("/upload_measured", HTTP_POST, []() {
		http_send_result(400, true, "Failed receiving file.");
	}, []() {
		HTTPUpload& upload = http->upload();

		switch (upload.status) {
		case UPLOAD_FILE_START:
			delivered_bytes = 0;

			if (!(upload_size = atoi(http->arg("size").c_str()))) {
				http_send_result(400, true, "No size "
				    "parameter passed. Perhaps your browser "
				    "failed to follow the 307 redirect "
				    "properly.");
				return;
			}

			if (upload_size > MAX_UPLOAD_SIZE) {
				http_send_result(400, true,
				    "File upload cannot be larger than %d "
				    "bytes.", MAX_UPLOAD_SIZE);
				return;
			}

			if (ms_write(upload_size & 0xff) != 0 ||
			    ms_write((upload_size >> 8) & 0xff) != 0) {
				http_send_result(400, true,
				    "Failed sending size bytes "
				    "(<tt>0x%x</tt>, <tt>0x%x</tt>) to "
				    "MailStation. Is the WSLoader program "
				    "running?",
				    upload_size & 0xff,
				    (upload_size >> 8) & 0xff);
				return;
			}
			break;
		case UPLOAD_FILE_WRITE:
			for (int i = 0; i < upload.currentSize; i++) {
				delivered_bytes++;
				if (ms_write(upload.buf[i]) == -1) {
					http_send_result(400, true,
					    "Failed uploading to MailStation "
					    "at byte %d/%d.",
					    delivered_bytes, upload_size);
					return;
				}
				yield();
				delayMicroseconds(500);
			}
			break;
		case UPLOAD_FILE_END:
			http_send_result(400, true,
			    "Successfully uploaded %d byte%s to MailStation.",
			    delivered_bytes, delivered_bytes == 1 ? "" : "s");
			return;
		}
	});

	http->onNotFound([]() {
		http_send_result(404, true, "404");
	});

	http->begin();
}
