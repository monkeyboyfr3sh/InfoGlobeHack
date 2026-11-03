# Font tracing

How `web/src/lib/font.js` was made from camera footage.

1. Extract a still per character-set group (full resolution, cropped to the text):
   `ffmpeg -ss <t> -i captures/PXL_20261102_035511574.mp4 -vf crop=1700:420:300:850 -frames:v 1 w_<group>.png`
2. `FIXROWS=79,120,163,206,246,286,327,368 FIXED=31.35,895,900 node trace.mjs w_40.png 1700 420`
   finds the dashes on each row and maps them to dot columns. The dome is modelled as a cylinder
   (pitch 31.35 px, axis at x=895, radius 900 px for this camera position).
   Output for every group: `traced-charset.txt`.
3. `node cells.mjs` slices the traces into 6-column cells (`glyphs.json`).
4. `node build_font.mjs` applies the hand corrections (glyphs squeezed at the picture edges, the p-w
   group's cell offset, accented letters) and writes the table pasted into `font.js`.
