# Featured by creators (the Welcome dialog)

`featured.json` lists the creator videos the Welcome dialog shows, at most three; an empty list hides the whole section.
Compiled into the app (cmake/EmbedFeatured.cmake), with the thumbnails next to it: nothing is fetched at run time, and a
click only opens the link in the browser.

```json
{"videos": [{"title": "Honest review", "creator": "Name", "url": "https://www.youtube.com/watch?v=...", "thumb": "name.jpg"}]}
```

- Only videos the creator has agreed to feature (their own honest review or tutorial); `url` is https.
- `thumb`: a 16:9 JPEG or PNG in this folder (about 320x180); new images go through `tools/check_marks_media.py`.
- Title and creator are plain text, cut to 40 characters.
