// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
package org.sculptforge.app;

import org.libsdl.app.SDLActivity;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

// SDL's activity runs the app (libmain.so's SDL_main, the same main() as on the desktop).
public class SculptForgeActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    // The update check's one request (src/app/AndroidHttp.cpp calls it): an https GET of the website's latest.json
    // or its GitHub copy, nothing else. No redirects, no cookies, no query, the same fixed user agent as the desktop
    // app (Android's own would name the phone). The system's TLS and certificate store check the server. Returns
    // "<status>\n<error>\n<body>": status 0 when the request never got an answer.
    public static String updateFeedGet(String url, int timeoutMs) {
        HttpURLConnection c = null;
        try {
            URL u = new URL(url);
            final String host = u.getHost();
            if (!"https".equals(u.getProtocol()) || u.getPort() != -1 || u.getQuery() != null || u.getUserInfo() != null
                    || !("sculptforge.app".equals(host) || "islandmed.github.io".equals(host)))
                return "0\nrefused: not the update feed\n";
            c = (HttpURLConnection) u.openConnection();
            c.setRequestMethod("GET");
            c.setInstanceFollowRedirects(false);
            c.setUseCaches(false);
            c.setConnectTimeout(timeoutMs);
            c.setReadTimeout(timeoutMs);
            c.setRequestProperty("User-Agent", "SculptForge/0.1");
            c.setRequestProperty("Accept", "application/json");
            final int status = c.getResponseCode();
            if (status != 200) return status + "\n\n";
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            try (InputStream in = c.getInputStream()) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                    if (out.size() > 1024 * 1024) return "0\nthe answer is too long\n"; // (latest.json is a few KB)
                }
            }
            return status + "\n\n" + new String(out.toByteArray(), StandardCharsets.UTF_8);
        } catch (Exception e) {
            return "0\n" + String.valueOf(e.getMessage()).replace('\n', ' ') + "\n";
        } finally {
            if (c != null) c.disconnect();
        }
    }
}
