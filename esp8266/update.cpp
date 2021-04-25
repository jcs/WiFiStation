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

#include <WiFiClientSecure.h>
#include "wifistation.h"

#define OTA_VERSION_URL "https://raw.githubusercontent.com/jcs/WiFiStation/main/release/version.txt"

WiFiClientSecure client;

bool
update_https_get_body(char *url, size_t expected_length)
{
	char *host, *path;
	char t[1];
	int status, httpver, chars, lines, tlength, clength;

	if (WiFi.status() != WL_CONNECTED) {
		output("ERROR WiFi is not connected\r\n");
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

	if (sscanf(url, "https://%[^/]%s%n", host, path, &chars) != 2 ||
	    chars == 0) {
		outputf("ERROR failed parsing URL %s\r\n", url);
		free(path);
		free(host);
		return false;
	}

	/*
	 * This would be nice to not have to do, but keeping up with GitHub's
	 * TLS cert fingerprints will be tedious, and we have no cert chain.
	 */
	client.setInsecure();

	if (!client.connect(host, 443)) {
		outputf("ERROR OTA failed connecting to %s:443\r\n", host);
		free(path);
		free(host);
		return false;
	 }

	client.printf("GET %s HTTP/1.0\r\n", path);
	client.printf("Host: %s\r\n", host);
	client.printf("User-Agent: wifistation %s\r\n", WIFISTATION_VERSION);
	client.printf("Connection: close\r\n");
	client.printf("\r\n");

	free(path);
	free(host);

	/* read headers */
	lines = 0;
	while (client.connected()) {
		String line = client.readStringUntil('\n');

		if (lines == 0)
			sscanf(line.c_str(), "HTTP/1.%d %d%n", &httpver,
			    &status, &chars);
		else if (sscanf(line.c_str(), "Content-Length: %d%n",
		    &tlength, &chars) == 1 && chars > 0) {
			clength = tlength;
		}

		if (line == "\r")
			break;

		lines++;
	}

	if (status != 200) {
		outputf("ERROR OTA fetch of %s failed with HTTP status %s\r\n",
		    url, status);
		goto drain;
	}

	if (expected_length != 0 && clength != expected_length) {
		outputf("ERROR OTA fetch of %s expected to be size %d, "
		    "fetched %d\r\n", url, expected_length, clength);
		goto drain;
	}

	return true;

drain:
	while (client.available())
		client.read();
	client.stop();
	return false;
}

void
update_process(bool do_update, bool force)
{
	String url, md5, version;
	char *host, *path;
	int bytesize;
	int lines = 0;
	size_t clength = 0;
	bool ret = true;

	if (!update_https_get_body(OTA_VERSION_URL, 0))
		return;

	lines = 0;
	while (client.available()) {
		String line = client.readStringUntil('\n');

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
			url = line;
			break;
		default:
#if DEBUG
			outputf("OTA unexpected line %d: %s\r\n", lines + 1,
			    line.c_str());
#endif
			break;
		}

		lines++;
	}

	client.stop();

	if (version == WIFISTATION_VERSION && !force) {
		outputf("ERROR OTA server reports version %s, no update "
		    "available\r\n", version.c_str());
		return;
	} else if (!do_update) {
		outputf("OK version %s (%d bytes) available, use AT$UPDATE "
		    "to update\r\n", version.c_str(), bytesize);
		return;
	}

	/* doing an update, parse the url */

	if (!update_https_get_body((char *)url.c_str(), bytesize))
		return;

	outputf("Updating to version %s (%d bytes) from %s\r\n",
	    version.c_str(), bytesize, (char *)url.c_str());

	Update.begin(bytesize, U_FLASH, pRedLED);

	Update.setMD5(md5.c_str());

	Update.onProgress([](unsigned int progress, unsigned int total) {
		outputf("\rFlash update progress: % 6u of % 6u", progress,
		    total);
	});

	if (Update.writeStream(client) != bytesize) {
		if (Update.getError() == UPDATE_ERROR_BOOTSTRAP)
			outputf("ERROR update must be done from fresh "
			    "reset, not from uploaded code\r\n");
		else
			outputf("ERROR failed writing download bytes: %d\r\n",
			    Update.getError());

		while (client.available())
			client.read();

		client.stop();
		return;
	}

	if (!Update.end()) {
		outputf("ERROR failed update at finish: %d\r\n",
		    Update.getError());
		return;
	}

	client.stop();
	outputf("\r\nOK update completed, restarting\r\n");

	delay(500);
	ESP.restart();
}
