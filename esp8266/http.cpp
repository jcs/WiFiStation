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
	</style>
</head>
<body>
<h3>WiFiStation %s</h3>
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

size_t
http_html_template(char **dest, char *body, ...)
{
	va_list arg;
	size_t len;
	char *tmp;

	/* expand body format */
	va_start(arg, body);
	len = vasprintf(&tmp, body, arg);
	va_end(arg);

	if (len == -1)
		goto fail;

	/* write body into template */
	len = asprintf(dest, html_wrapper, WiFi.localIP().toString().c_str(),
	    WIFISTATION_VERSION, tmp);
	if (len == -1) {
		free(tmp);
		goto fail;
	}

	return len;

fail:
	http->send(500, "text/plain", "out of memory :(");
	return -1;
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

	http->on("/", HTTP_GET, []() {
		char *doc = NULL;

		http_html_template(&doc, R"END(
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
		if (doc == NULL)
			return;
		http->send(200, "text/html", doc);
		free(doc);
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
		http->send(200);
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
			snprintf(tmp, sizeof(tmp), "/upload_measured?size=%d",
			    upload_size);
			http->sendHeader("Location", tmp);
			http->send(307);
			break;
		}
	});

	http->on("/upload_measured", HTTP_POST, []() {
		http->send(200);
	}, []() {
		HTTPUpload& upload = http->upload();
		char *doc = NULL;

		switch (upload.status) {
		case UPLOAD_FILE_START:
			delivered_bytes = 0;

			if (!(upload_size = atoi(http->arg("size").c_str()))) {
				http_html_template(&doc,
				    "No size parameter passed. Perhaps your "
				    "browser failed to follow the 307 "
				    "redirect properly."
				    "<p>"
				    "<a href=\"/\">Home</a>");
				if (doc == NULL)
					return;
				http->send(400, "text/html", doc);
				free(doc);
				return;
			}

			if (upload_size > MAX_UPLOAD_SIZE) {
				http_html_template(&doc,
				    "File upload cannot be larger than %d."
				    "<p>"
				    "<a href=\"/\">Home</a>",
				    MAX_UPLOAD_SIZE);
				if (doc == NULL)
					return;
				http->send(400, "text/html", doc);
				free(doc);
				return;
			}

			if (ms_write(upload_size & 0xff) != 0 ||
			    ms_write((upload_size >> 8) & 0xff) != 0) {
				http_html_template(&doc,
				    "Failed sending size bytes "
				    "(<tt>0x%x</tt>, <tt>0x%x</tt>) to "
				    "MailStation. Is the WSLoader program "
				    "running?"
				    "<p>"
				    "<a href=\"/\">Home</a>",
				    upload_size & 0xff,
				    (upload_size >> 8) & 0xff);
				if (doc == NULL)
					return;
				http->send(400, "text/html", doc);
				free(doc);
				return;
			}
			break;
		case UPLOAD_FILE_WRITE:
			for (int i = 0; i < upload.currentSize; i++) {
				delivered_bytes++;
				if (ms_write(upload.buf[i]) == -1) {
					http_html_template(&doc,
					    "Failed uploading to MailStation "
					    "at byte %d/%d."
					    "<p>"
					    "<a href=\"/\">Home</a>",
					    delivered_bytes, upload_size);
					if (doc == NULL)
						return;
					http->send(400, "text/html", doc);
					free(doc);
					return;
				}
				yield();
			}
			break;
		case UPLOAD_FILE_END:
			http_html_template(&doc,
			    "Successfully uploaded %d byte%s to MailStation."
			    "<p>"
			    "<a href=\"/\">Home</a>",
			    delivered_bytes, delivered_bytes == 1 ? "" : "s");
			if (doc == NULL)
				return;
			http->send(200, "text/html", doc);
			free(doc);
			break;
		}
	});

	http->onNotFound([]() {
		http->send(404, "text/plain", ":(");
	});

	http->begin();
}
