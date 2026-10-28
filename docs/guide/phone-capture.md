# Phone capture and AR

Your phone becomes SculptForge's camera and second screen. Scan an object by walking around it, send a photo to
turn into a model, bring in a splat from a scanning app, look at your sculpt standing on your real desk in AR,
and drive a character's face with your own. There is no app to install and no account: the phone's browser talks
to SculptForge over your Wi-Fi.

## Pair your phone

1. Choose **File > Capture from phone** (or the phone button in the LightBox's header). A window opens with a QR code.
2. Point the phone's camera at the QR code and tap the link it shows. The capture page opens, already paired with
   this window: the badge at its top shows your computer's name.

The phone and the computer must be on the same Wi-Fi. If the phone has no QR scanner, open the address shown under
the code in its browser and type the 6-digit pairing code.

The link works only while the window is open: closing it stops SculptForge listening, and the next time you open it
there is a new code. A code nobody uses expires after 10 minutes.

> **Tip:** The phone can't open the page? Check that it isn't on a guest network or mobile data, turn off VPNs on
> both devices, and allow SculptForge through the firewall (Windows asks the first time; choose private networks).
> If the computer has several networks, pick the Wi-Fi one under **Network** in the window.

### "This site can't be reached" on Android

Recent versions of Chrome on Android can refuse to open any address on your home network, even your router's,
and show **This site can't be reached** (under Details: `ERR_TOO_MANY_RETRIES`). Until SculptForge's capture page
moves to the website, the simplest fix is to **use Firefox on the phone**: install it from the Play Store, open the
address shown under the QR code (for example `http://10.0.0.117:8765`), allow it to reach devices on your local
network when it asks, and type the 6-digit code. Firefox can also be set as the phone's default browser so the
camera's QR links open there.

### Linux firewall

Many Linux desktops (Omarchy, Ubuntu with ufw enabled, Fedora with firewalld) block incoming connections, so the
phone's request never arrives. Allow SculptForge's port from your home network only, TCP for the page and UDP for
the direct link that AR on Android and face capture use, for example with ufw:

```
sudo ufw allow from 10.0.0.0/24 to any port 8765 comment 'SculptForge phone capture'
```

Use your own network's range (the first three numbers of the address under the QR code, then `.0/24`). With
firewalld: `sudo firewall-cmd --add-port=8765/tcp --add-port=8765/udp` (add `--permanent` to keep it).

## Scan an object

Tap **Scan an object**, read the tips, and tap **Record a video**. Walk slowly all the way round the object, then
once more a little lower, for 20 to 60 seconds:

- slow and steady, no quick turns;
- soft, even light, without hard shadows or shiny reflections;
- the whole object in the middle of the screen, at about the same distance all the way (don't zoom).

You can also send a video you already took, or 20 to 200 photos (**Choose photos**). The page warns you when a
video is very short or very long. Tap **Send to SculptForge**: the upload shows its progress on the phone and in
SculptForge's window.

Big videos over shaky Wi-Fi are fine: the page sends them in pieces and carries on where it stopped if the
connection drops. Keep the page open and the screen on until it says **Sent**.

What arrives is saved in your library's **Captures** folder (see below) and handed to splat training, which turns
it into a Gaussian splat that sharpens in the viewport while it trains. See [Gaussian splats](splats.md).

## One photo to 3D

Tap **One photo to 3D** and take a photo (or choose one). One object, the whole of it in the picture, on a plain
background works best. SculptForge loads it into **Generate > Image to 3D** and starts generating with the service
chosen there (free Hugging Face models by default). Turn off **Generate from a photo at once** in the window to
check the photo first and press **Generate** yourself. See [Image and text to 3D](generate-3d.md).

The phone sends large photos at about 2048 pixels, which is plenty and much faster.

## Send a splat file

Already scanned it with Scaniverse, Polycam or Luma? Export a Gaussian splat `.ply` from the app, tap **Send a splat
file** and choose it. It opens next to your sculpt as a splat. A `.ply` that is a plain mesh becomes a new SubTool
instead, so it never replaces your work.

## View in AR

Tap **View in AR**, choose what to show (the **Active SubTool** or the **Whole scene**) and how big it should be on
the desk (10 cm to 1 m), then **Get the model**. SculptForge prepares a phone-friendly copy: reduced to about 80,000
triangles, its polypaint or texture baked into one texture, standing on its base. When it is ready, tap **View in
AR** and point the phone at a desk or the floor.

- **iPhone and iPad** open it in AR Quick Look (a USDZ file).
- **Android** opens an AR page in **Chrome** that gets the model straight from your computer over Wi-Fi (it needs
  Google Play Services for AR, which most phones have). Tap **Place it in your room**, move the phone slowly over
  the desk until it finds the surface, and tap to place. Firefox can't show AR: if the capture page is open in
  Firefox, the AR page offers **Copy the link**: paste it into Chrome. **Download the model** saves the file instead.

Keep sculpting: while the phone is looking, every few seconds after a stroke SculptForge exports the model again
and the page says **Updated from your sculpt**. On iPhone, tap **View in AR** again to see the change; Android's AR
page updates by itself. Turn off **Update after
each stroke** in the window to stop that.

## Face capture

Tap **Face capture**, then **Start face capture**. Your phone's front camera tracks your face with MediaPipe Face
Landmarker, which runs on the phone: the video never leaves it. About 30 times a second only the 52 expression
weights (the ARKit set: jawOpen, eyeBlinkLeft, mouthSmileRight...) and the head's rotation go to SculptForge.

Prop the phone up at eye level, about an arm's length away, with light on your face. The window's **Face** section
shows a little face following yours and the frame rate.

The weights drive the character's facial shapes on the active SubTool (made with **Generate facial shapes**).
If the character winks with the wrong eye, tick **Mirror left and right**.

**Record face take** records the feed until you press it again. Each take is saved in the Captures folder as
`take.jsonl`: one line per frame with the 52 weights, the head rotation and the time, ready to play back or export.

> **Note:** The camera only works on secure (HTTPS) pages, so the face page lives on the SculptForge website. The
> address it opens carries the connection details after a `#`, which browsers never send to the website. To set up
> the connection the phone sends one encrypted message through SculptForge's relay (or ntfy.sh, a free public
> relay, if ours can't be reached); neither can read it. Without internet, tap **Connect without internet** on the
> face page. After that the face data goes straight over your Wi-Fi.

## The Captures folder

Everything the phone sends is saved in your library (the folder the LightBox shows, normally
`Documents/SculptForge`), under `Captures`, one folder per capture:

```
Captures/2026-10-26_195803-splat/scan.mov
Captures/2026-10-26_195803-splat/capture.json
Captures/2026-10-26_201544-photo/IMG_0412.jpg
Captures/2026-10-26_203012-face/take.jsonl
```

`capture.json` records the kind (`splat`, `photo`, `ply` or `face`), the device, the files and the times. The
window's **Received** list opens a capture's folder.

## Privacy and security

- Only devices on your local network can connect, and only while the window is open.
- Every request needs the session's secret token (it is in the QR code) or the pairing code.
- Uploads have size limits (4 GB a file, 8 GB a capture), file names are cleaned, and files are written only inside
  the Captures folder.
- Nothing you capture is uploaded anywhere except to your computer. A photo goes on to the image-to-3D service you
  chose in SculptForge, as it does when you pick it yourself.
