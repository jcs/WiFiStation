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

#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "wifistation.h"

static const char OTA_VERSION_URL[] PROGMEM =
    "https://raw.githubusercontent.com/jcs/WiFiStation/main/release/version.txt";

static WiFiClient client;
static WiFiClientSecure client_tls;
static bool tls = false;

bool
update_http_read_until_body(const char *url, long expected_length)
{
	char *host, *path, *colon;
	int status = 0, port, httpver, chars, lines, tlength, clength = -1;

	if (WiFi.status() != WL_CONNECTED) {
		output("ERROR WiFi is not connected\r\n");
		return false;
	}

	if (url == NULL) {
		outputf("ERROR failed parsing NULL URL\r\n");
		return false;
	}

	host = (char *)malloc(strlen(url) + 1);
	if (host == NULL) {
		output("ERROR malloc failed\r\n");
		return false;
	}

	path = (char *)malloc(strlen(url) + 1);
	if (path == NULL) {
		output("ERROR malloc failed\r\n");
		free(host);
		return false;
	}

	if (sscanf(url, "http://%[^/]%s%n", host, path, &chars) == 2 &&
	    chars != 0) {
		tls = false;
	} else if (sscanf(url, "https://%[^/]%s%n", host, path, &chars) == 2
	    && chars != 0) {
		tls = true;
		/*
		 * This would be nice to not have to do, but keeping up with
		 * GitHub's TLS cert fingerprints will be tedious, and we have
		 * no cert chain.
		 */
		client_tls.setInsecure();
	} else {
		outputf("ERROR failed parsing URL \"%s\"\r\n", url);
		free(path);
		free(host);
		return false;
	}

	if ((colon = strchr(host, ':'))) {
		colon[0] = '\0';
		port = atoi(colon + 1);
	} else {
		port = (tls ? 443 : 80);
	}

#ifdef UPDATE_TRACE
	syslog.logf(LOG_DEBUG, "%s: host \"%s\" path \"%s\" port %d tls %d",
	    __func__, host, path, port, tls ? 1 : 0);
#endif

	if (!(tls ? client_tls : client).connect(host, port)) {
		outputf("ERROR OTA failed connecting to http%s://%s:%d\r\n",
		    tls ? "s" : "", host, port);
		free(path);
		free(host);
		return false;
	}

	(tls ? client_tls : client).printf("GET %s HTTP/1.0\r\n", path);
	(tls ? client_tls : client).printf("Host: %s\r\n", host);
	(tls ? client_tls : client).printf("User-Agent: WiFiStation %s\r\n",
	    WIFISTATION_VERSION);
	(tls ? client_tls : client).printf("Connection: close\r\n");
	(tls ? client_tls : client).printf("\r\n");

	free(path);
	free(host);

	/* read headers */
	lines = 0;
	while ((tls ? client_tls : client).connected() ||
	    (tls ? client_tls : client).available()) {
		String line = (tls ? client_tls : client).readStringUntil('\n');

#ifdef UPDATE_TRACE
		syslog.logf(LOG_DEBUG, "%s: read header \"%s\"", __func__,
		    line.c_str());
#endif

		if (lines == 0)
			sscanf(line.c_str(), "HTTP/1.%d %d%n", &httpver,
			    &status, &chars);
		else if (sscanf(line.c_str(), "Content-Length: %d%n",
		    &tlength, &chars) == 1 && chars > 0)
			clength = tlength;
		else if (line == "\r")
			break;

		lines++;
	}

#ifdef UPDATE_TRACE
	syslog.logf(LOG_DEBUG, "%s: read status %d, content-length %d vs "
	    "expected %ld", __func__, status, clength, expected_length);
#endif

	if (status != 200) {
		outputf("ERROR OTA fetch of %s failed with HTTP status %d\r\n",
		    url, status);
		goto drain;
	}

	if (expected_length != 0 && clength != expected_length) {
		outputf("ERROR OTA fetch of %s expected to be size %d, "
		    "fetched %d\r\n", url, expected_length, clength);
		goto drain;
	}

	while ((tls ? client_tls : client).connected() &&
	    !(tls ? client_tls : client).available())
		ESP.wdtFeed();

	return true;

drain:
#ifdef UPDATE_TRACE
	syslog.logf(LOG_DEBUG, "%s: draining remaining body", __func__);
#endif
	while ((tls ? client_tls : client).available())
		(tls ? client_tls : client).read();
	(tls ? client_tls : client).stop();
	return false;
}

void
update_process(char *url, bool do_update, bool force)
{
	String rom_url, md5, version;
	int bytesize = 0, lines = 0, len;
	char *furl = NULL;

	output("\n");

	if (url == NULL) {
		furl = url = (char *)malloc(len = (strlen_P(OTA_VERSION_URL) +
		    1));
		if (url == NULL) {
			output("ERROR malloc failed\r\n");
			return;
		}
		memcpy_P(url, OTA_VERSION_URL, len);
	}

#ifdef UPDATE_TRACE
	syslog.logf(LOG_DEBUG, "fetching update manifest from \"%s\"", url);
#endif

	if (!update_http_read_until_body(url, 0)) {
#ifdef UPDATE_TRACE
		syslog.logf(LOG_DEBUG, "read until body(%s) failed", url);
#endif
		if (furl)
			free(furl);
		return;
	}
	if (furl)
		free(furl);

	lines = 0;
	while ((tls ? client_tls : client).connected() ||
	    (tls ? client_tls : client).available()) {
		String line = (tls ? client_tls : client).readStringUntil('\n');

#ifdef UPDATE_TRACE
		syslog.logf(LOG_DEBUG, "%s: read body[%d] \"%s\"", __func__,
		    lines, line.c_str());
#endif

		switch (lines) {
		case 0:
			version = line;
			break;
		case 1:
			bytesize = atoi(line.c_str());
			break;
		case 2:
			md5 = line;
			break;
		case 3:
			rom_url = line;
			break;
		default:
#ifdef UPDATE_TRACE
			syslog.logf("%s: unexpected line %d: %s\r\n", __func__,
			    lines + 1, line.c_str());
#endif
			break;
		}

		lines++;
	}

	(tls ? client_tls : client).stop();

	if (version == WIFISTATION_VERSION && !force) {
		outputf("ERROR OTA server reports version %s, no update "
		    "available\r\n", version.c_str());
		return;
	} else if (!do_update) {
		outputf("OK version %s (%d bytes) available, use AT$UPDATE "
		    "to update\r\n", version.c_str(), bytesize);
		return;
	}

	/* doing an update, parse the url read */
#ifdef UPDATE_TRACE
	syslog.logf(LOG_DEBUG, "%s: doing update with ROM url \"%s\" size %d",
	    __func__, rom_url.c_str(), bytesize);
#endif
	if (!update_http_read_until_body((char *)rom_url.c_str(), bytesize))
		return;

	outputf("Updating to version %s (%d bytes) from %s\r\n",
	    version.c_str(), bytesize, (char *)rom_url.c_str());

	Update.begin(bytesize, U_FLASH, pRedLED);

	Update.setMD5(md5.c_str());

	Update.onProgress([](unsigned int progress, unsigned int total) {
		outputf("\rFlash update progress: % 6u of % 6u", progress,
		    total);
	});

	if ((int)Update.writeStream((tls ? client_tls : client)) != bytesize) {
		switch (Update.getError()) {
		case UPDATE_ERROR_BOOTSTRAP:
			outputf("\nERROR update must be done from fresh "
			    "reset, not from uploaded code\r\n");
			break;
		case UPDATE_ERROR_MAGIC_BYTE:
			outputf("\nERROR image does not start with 0xE9\r\n");
			break;
		default:
			outputf("\nERROR failed writing download bytes: %d\r\n",
			    Update.getError());
		}

		while ((tls ? client_tls : client).available())
			(tls ? client_tls : client).read();

		(tls ? client_tls : client).stop();
		return;
	}

	if (!Update.end()) {
		outputf("\nERROR failed update at finish: %d\r\n",
		    Update.getError());
		return;
	}

	(tls ? client_tls : client).stop();
	outputf("\r\nOK update completed, restarting\r\n");

	delay(500);
	ESP.restart();
}
