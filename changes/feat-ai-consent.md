---
whats_new: **Nothing leaves without your OK**: the first time AI Brush or Generate 3D would send anything to a service
  on someone else's computer, the panel says what is sent, to which address and who pays, and waits for **Allow**
  (once per service). **Use local instead** switches to a model running on this computer. Revoke any service in
  **Edit > Preferences > AI > Online AI services**.
highlight: Each online AI service asks once before anything is sent
added: A first-use notice before the first request to each third-party AI host (Pollinations.ai, each Hugging Face
  Space family, fal.ai, Meshy, Hyper3D, a remote OpenAI-compatible server or ComfyUI), drawn inside the AI Brush or
  Generate 3D panel (no dialog): what is sent, the host, who pays, and **Allow** / **Use local instead** (a running
  Ollama, LM Studio or ComfyUI) or **Set up local** / **Cancel**. Nothing is sent before Allow: the request functions
  refuse on their own, so scripts, `sculptforge-cli` and AI tools get the same refusal until the service is allowed
  in the app. Allow is kept per service in `ai.json` (`sendAllowed`); **Preferences > AI > Online AI services** lists
  each one as Allowed, Asks first, or set by the studio, with **Revoke**. Servers on this computer (localhost,
  127.0.0.1, ::1) never ask. A studio policy pre-allows what its `ai.providers` / `ai.endpoints` list and its "no"
  wins over an artist's Allow. The guide's interface chapter has a plain "What leaves your computer" table. No new
  network host, key or server.
testers:
  - Fresh settings (or remove `sendAllowed` from ai.json): Generate 3D on a picture, Generate: the panel shows "Send
    to Hugging Face (Tencent)?" with the image, the host and "Free". Cancel: nothing is generated.
  - Generate again, Allow: it runs (the Hunyuan3D licence note follows), and the next Generate goes straight on.
  - AI Brush (Free): Create shows "Send to Pollinations.ai?"; Allow makes the brush; the next one doesn't ask.
  - With Ollama running: the notice's middle button says Use local instead; it switches the provider to Ollama and
    makes the brush there (nothing leaves the computer). Without Ollama: Set up local opens the guide.
  - Preferences > AI > Online AI services: the allowed ones say Allowed with Revoke; Revoke, and the next request asks again.
  - Windows at 150% / 200%: the notice's three buttons and the services table fit the panels; Spanish and Chinese read well.
---
Brief: privacy default for AI features. Today's defaults sent the brush description to Pollinations and images to
public Hugging Face Spaces with no per-service notice; now each third-party host asks once.
