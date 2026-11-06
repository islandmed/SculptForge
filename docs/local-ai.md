# Run AI on your own computer

Brush Lab works free out of the box with a hosted model. If you'd rather keep everything on your
machine (private, offline, no rate limits), run a local model with [Ollama](https://ollama.com) or
[LM Studio](https://lmstudio.ai). Both are free, and both work on Windows, macOS and Linux.

It takes about five minutes: install, download one model (2.5 GB), and point SculptForge at it.

## What you need

| | Minimum | Comfortable |
|---|---|---|
| Memory (RAM) | 8 GB | 16 GB or more |
| Disk space | 3 GB for the model | |
| Graphics card | Not required | Any recent NVIDIA, AMD or Apple Silicon GPU |
| Time per brush | 10-20 s on a laptop CPU | 2-5 s with a GPU |

The model we recommend is **`qwen3:4b-instruct`** (2.5 GB). It's the one we test Brush Lab with,
and it reliably produces brush recipes. With 16 GB or more you can try a larger model such as
`qwen3:8b` for more inventive brushes. On a machine with less than 8 GB, stick with the free hosted
provider.

## 1. Install Ollama

### Windows (10 or 11)

1. Download **OllamaSetup.exe** from [ollama.com/download](https://ollama.com/download) and run it.
   No administrator rights are needed.

   Or install it from PowerShell, using either of these:

   ```powershell
   winget install Ollama.Ollama
   irm https://ollama.com/install.ps1 | iex
   ```

2. Ollama starts by itself and shows a llama icon in the system tray (bottom-right, near the clock).
   It starts automatically when you sign in.
3. Open **PowerShell** or **Terminal** from the Start menu and download the model:

   ```powershell
   ollama pull qwen3:4b-instruct
   ```

NVIDIA and AMD Radeon GPUs are used automatically. Keep your graphics driver up to date.

### macOS (Apple Silicon or Intel)

1. Download **Ollama.dmg** from [ollama.com/download](https://ollama.com/download), open it and drag
   Ollama into **Applications**. Open it once; it lives in the menu bar (top-right).

   Or, with [Homebrew](https://brew.sh):

   ```sh
   brew install --cask ollama   # the app (starts at login)
   # or: brew install ollama && brew services start ollama   (command-line only)
   ```

2. Open **Terminal** (Applications → Utilities) and download the model:

   ```sh
   ollama pull qwen3:4b-instruct
   ```

Apple Silicon Macs (M1 and later) use the GPU automatically and are fast. Intel Macs run on the CPU.

### Linux

1. Install with the official script (it sets up a `systemd` service and detects NVIDIA/AMD GPUs):

   ```sh
   curl -fsSL https://ollama.com/install.sh | sh
   ```

   Or use your distribution's package:

   | Distribution | Command |
   |---|---|
   | Arch / Manjaro | `sudo pacman -S ollama` (or `ollama-cuda` for NVIDIA, `ollama-rocm` for AMD) |
   | NixOS | add `services.ollama.enable = true;` to your configuration |

2. Make sure the service is running. The script starts it for you; distribution packages may not:

   ```sh
   sudo systemctl enable --now ollama
   systemctl status ollama        # should say "active (running)"
   ```

   No systemd? Run `ollama serve` in a terminal and leave it open.

3. Download the model:

   ```sh
   ollama pull qwen3:4b-instruct
   ```

## 2. Check that it's running

In any terminal (PowerShell on Windows):

```sh
ollama list                      # shows qwen3:4b-instruct
ollama run qwen3:4b-instruct "Say hi"
```

Ollama listens on `http://localhost:11434`. Opening that address in a browser should show
"Ollama is running".

## 3. Connect SculptForge

1. Open **Brush Lab** and expand **AI provider**.
2. Choose **Ollama (this computer)**: the address is filled in.
3. Click **Find models**. You should see "Connected - 1 model installed", and the model appears in the **Model**
   list.
4. Click **Save settings**, then describe a brush: "dry cracked mud".

Leave **API key** empty. Local servers don't need one.

From the command line:

```sh
sculptforge-cli -c 'brush_ai prompt="dry cracked mud" provider=openai-compatible model=qwen3:4b-instruct; brush_save path=mud.sfbrush'
```

Even if you stay on the **Free** provider, SculptForge uses your local Ollama automatically whenever
the hosted model can't be reached (for example, when you're offline). It picks your smallest
installed model.

## Using LM Studio instead

[LM Studio](https://lmstudio.ai) is a point-and-click app for Windows, macOS and Linux (AppImage),
if you'd rather not use a terminal.

1. Install LM Studio and open it.
2. Search for **Qwen3 4B Instruct** in the **Discover** tab and download it.
3. Open the **Developer** tab, load the model and switch the server **on** (it serves on port 1234).
4. In SculptForge choose **LM Studio (this computer)**, then **Find models**.

## Running the model on another computer

You can run Ollama on a stronger machine (say, a desktop with a GPU) and sculpt on a laptop. On the
machine running Ollama, set `OLLAMA_HOST=0.0.0.0:11434` and restart Ollama:

- **Windows:** quit Ollama from the tray, add `OLLAMA_HOST` under *Edit environment variables for
  your account*, then start Ollama again.
- **macOS:** `launchctl setenv OLLAMA_HOST "0.0.0.0:11434"`, then quit and reopen Ollama.
- **Linux:** run `sudo systemctl edit ollama`, add these lines, then run
  `sudo systemctl restart ollama`:

  ```ini
  [Service]
  Environment="OLLAMA_HOST=0.0.0.0:11434"
  ```

In SculptForge, set the endpoint to `http://<that-computer's-IP>:11434/v1/chat/completions`. Only
do this on a network you trust, because anyone on it can then use your model.

## Troubleshooting

| You see | Fix |
|---|---|
| "Nothing is answering at http://localhost:11434..." | Ollama isn't running. Windows/macOS: start the Ollama app. Linux: `sudo systemctl start ollama`, or run `ollama serve`. |
| "The server is running but has no models yet" | Run `ollama pull qwen3:4b-instruct`. |
| "HTTP 404 ... model not found" | The model name in settings doesn't match `ollama list`. Click **Find models** and pick it from the list. |
| "The model did not return a brush recipe" | Very small models (1B and under) sometimes ignore the format. Use `qwen3:4b-instruct` or larger, or try again. |
| The first brush takes much longer | The model loads into memory on first use. Later requests are faster. Ollama unloads it after 5 idle minutes. |
| It's very slow | You're running on the CPU. That's fine for Brush Lab (one short reply per brush), but a GPU or Apple Silicon is much faster. Close other heavy apps to free memory. |
| Port 11434 already in use (Linux) | The service is already running, so you don't need `ollama serve`. |

## Privacy

With a local model, nothing about your brush requests leaves your computer. Ollama and LM Studio
download models once, then work fully offline. See [AI services & privacy](ai-services.md) for what
each provider receives.
