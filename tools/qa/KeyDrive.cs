// Real-input key check for the Windows PC: injects keys into the focused SculptForge window through user32 SendInput, so
// they travel the real Win32 -> SDL -> App path (the UI tests' key_down skips it). Built on the PC with csc.exe as
// sculptforge.exe in its own folder so `win.sh run keydrv ARGS` (desktop session, real GPU) starts it:
//   csc /nologo /out:C:\sf\keydrv\sculptforge.exe KeyDrive.cs     (tools/qa/keydrive.sh does this)
// Args: REAL_EXE OUT_DIR [case ...]   cases: tap (control) | together | vfirst | slowroll | slowv | held | rolling | pick | switch | fill (default: tap, together, vfirst, rolling)
// For each case it starts a fresh app with a private user-data dir whose ui.json binds Fill = "C then V", C = Pick colour,
// V = Switch Color (SculptForge keys), sends the keys, and writes OUT_DIR\<case>.png plus a line in OUT_DIR\keydrive.txt:
// the model's mean colour before and after, and SF_KEY_LOG's lines (what the app saw), if the build has it.
using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

static class KeyDrive {
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Explicit)] struct INPUTU { [FieldOffset(0)] public KEYBDINPUT ki; [FieldOffset(0)] public MOUSEINPUT mi; }
    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public INPUTU u; }
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] static extern void mouse_event(uint flags, int dx, int dy, uint data, IntPtr extra);

    const uint KEYEVENTF_KEYUP = 2, KEYEVENTF_SCANCODE = 8;
    const ushort VK_C = 0x43, VK_V = 0x56;

    static INPUT Key(ushort vk, bool down) {
        var i = new INPUT { type = 1 }; // INPUT_KEYBOARD: scan code + virtual key, as a real keyboard's
        i.u.ki = new KEYBDINPUT { wVk = vk, wScan = (ushort)MapVirtualKey(vk, 0), dwFlags = down ? 0u : KEYEVENTF_KEYUP };
        return i;
    }
    static void Send(params INPUT[] inputs) { SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(INPUT))); }

    static void Run(string name, IntPtr h) {
        switch (name) {
        case "together": // (a) C-down and V-down in ONE SendInput call (one timestamp), then the releases
            Send(Key(VK_C, true), Key(VK_V, true)); Thread.Sleep(80); Send(Key(VK_C, false), Key(VK_V, false)); break;
        case "vfirst": // (b) V-down then C-down
            Send(Key(VK_V, true)); Send(Key(VK_C, true)); Thread.Sleep(80); Send(Key(VK_V, false)); Send(Key(VK_C, false)); break;
        case "rolling": // (c) C-down, 30 ms, V-down, then the releases (C first: a rolling finger)
            Send(Key(VK_C, true)); Thread.Sleep(30); Send(Key(VK_V, true)); Thread.Sleep(80); Send(Key(VK_C, false)); Send(Key(VK_V, false)); break;
        case "slowroll": // C-down, 300 ms (inside the 0.4 s window), V-down, held a full second
            Send(Key(VK_C, true)); Thread.Sleep(300); Send(Key(VK_V, true)); Thread.Sleep(1000); Send(Key(VK_C, false)); Send(Key(VK_V, false)); break;
        case "slowv": // V-down, 300 ms, C-down, held a full second
            Send(Key(VK_V, true)); Thread.Sleep(300); Send(Key(VK_C, true)); Thread.Sleep(1000); Send(Key(VK_V, false)); Send(Key(VK_C, false)); break;
        case "held": // both down together and held 1 s with the keyboard's auto-repeat on the last key (a repeat = a key-down again)
            Send(Key(VK_C, true), Key(VK_V, true));
            for (int i = 0; i < 30; i++) { Thread.Sleep(33); Send(Key(VK_V, true)); }
            Send(Key(VK_C, false), Key(VK_V, false)); break;
        case "pick": Send(Key(VK_C, true)); Thread.Sleep(60); Send(Key(VK_C, false)); break;   // C alone: Pick colour
        case "switch": Send(Key(VK_V, true)); Thread.Sleep(60); Send(Key(VK_V, false)); break; // V alone: Switch Color
        case "tap": Send(Key(VK_C, true)); Thread.Sleep(40); Send(Key(VK_C, false)); Thread.Sleep(120); Send(Key(VK_V, true)); Thread.Sleep(40); Send(Key(VK_V, false)); break;
        }
    }

    // Mean RGB of the middle of the window (the model).
    static double[] Mean(Bitmap b) {
        long r = 0, g = 0, bl = 0, n = 0;
        int x0 = b.Width * 38 / 100, x1 = b.Width * 50 / 100, y0 = b.Height * 62 / 100, y1 = b.Height * 78 / 100; // (the sphere: its lower middle)
        for (int y = y0; y < y1; y += 2) for (int x = x0; x < x1; x += 2) { var c = b.GetPixel(x, y); r += c.R; g += c.G; bl += c.B; n++; }
        return new[] { r / (double)n, g / (double)n, bl / (double)n };
    }

    // Latency: how long after a key goes down the Switch Color swap shows in the colour swatches (polled on screen, ~2 ms).
    // latcombo: V is part of "C then V" (waits the Two-key window); latcombo35: the same with the window at its shortest;
    // latplain: Switch Color on B, which is in no two-key shortcut (the baseline: the app's own frame latency).
    static string Latency(string name, IntPtr h) {
        ushort vk = name == "latplain" ? (ushort)0x42 : VK_V;
        RECT rc; GetWindowRect(h, out rc);
        int x = rc.L + (int)((rc.R - rc.L) * 79.0 / 1936), y = rc.T + (int)((rc.B - rc.T) * 650.0 / 1048);
        var probe = new Bitmap(6, 6);
        Func<int> sample = () => { using (var g = Graphics.FromImage(probe)) g.CopyFromScreen(x - 3, y - 3, 0, 0, probe.Size); var c = probe.GetPixel(3, 3); return c.R * 65536 + c.G * 256 + c.B; };
        var ms = new System.Collections.Generic.List<double>();
        for (int i = 0; i < 8; i++) {
            int before = sample();
            var sw = Stopwatch.StartNew();
            Send(Key(vk, true));
            double dt = -1;
            while (sw.ElapsedMilliseconds < 2500) { if (sample() != before) { dt = sw.Elapsed.TotalMilliseconds; break; } }
            Thread.Sleep(30); Send(Key(vk, false));
            ms.Add(dt);
            Thread.Sleep(1200); // (settled, and the next press is far from this one)
        }
        return string.Join(" ", ms.ConvertAll(v => v < 0 ? "none" : v.ToString("F0")));
    }

    static Bitmap Shot(IntPtr h, string path) {
        RECT rc; GetWindowRect(h, out rc);
        var bmp = new Bitmap(rc.R - rc.L, rc.B - rc.T);
        using (var g = Graphics.FromImage(bmp)) g.CopyFromScreen(rc.L, rc.T, 0, 0, bmp.Size);
        if (path != null) bmp.Save(path, ImageFormat.Png);
        return bmp;
    }

    static int Main(string[] a) {
        SetProcessDPIAware();
        if (a.Length < 2) { Console.Error.WriteLine("KeyDrive REAL_EXE OUT_DIR [case...]"); return 2; }
        string exe = a[0], outDir = a[1];
        Directory.CreateDirectory(outDir);
        var cases = a.Length > 2 ? new string[a.Length - 2] : new[] { "tap", "together", "vfirst", "rolling" };
        if (a.Length > 2) Array.Copy(a, 2, cases, 0, cases.Length);
        var report = new StringBuilder();
        foreach (var name in cases) {
            string data = Path.Combine(outDir, "data-" + name);
            string cfg = Path.Combine(data, "SculptForge", "SculptForge");
            if (Directory.Exists(data)) Directory.Delete(data, true);
            Directory.CreateDirectory(cfg);
            // Fill = "C then V", C = Pick colour, V = Switch Color; the main colour red, so Fill is plain to see.
            File.WriteAllText(Path.Combine(cfg, "ui.json"),
                name == "latplain" ? "{\"version\":1,\"shortcuts\":{\"color.switch\":\"B\"},\"prefs\":{}}"
                : name == "latcombo35" ? "{\"version\":1,\"shortcuts\":{\"color.fill\":\"C then V\",\"color.pick\":\"C\",\"color.switch\":\"V\"},\"prefs\":{\"twoKeyWindow\":0.35}}"
                : "{\"version\":1,\"shortcuts\":{\"color.fill\":\"C then V\",\"color.pick\":\"C\",\"color.switch\":\"V\"},\"prefs\":{}}");
            string keylog = Path.Combine(outDir, "keylog-" + name + ".txt");
            if (File.Exists(keylog)) File.Delete(keylog);
            var psi = new ProcessStartInfo(exe) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(exe) };
            psi.EnvironmentVariables["SF_USER_DATA_DIR"] = data;
            psi.EnvironmentVariables["SF_KEY_LOG"] = keylog;
            psi.EnvironmentVariables["SF_ISOLATE"] = "0";
            var p = Process.Start(psi);
            IntPtr h = IntPtr.Zero;
            for (int i = 0; i < 120 && h == IntPtr.Zero; i++) { Thread.Sleep(500); p.Refresh(); h = p.MainWindowHandle; }
            if (h == IntPtr.Zero) { report.AppendLine(name + ": NO WINDOW"); try { p.Kill(); } catch { } continue; }
            Thread.Sleep(9000); // (first frame, the default model, the welcome popup if any)
            ShowWindow(h, 3);
            SetForegroundWindow(h); Thread.Sleep(1500);
            if (GetForegroundWindow() != h) { // never send clicks and keys into whatever else has the desktop
                report.AppendLine(name + ": NOT FOREGROUND (case skipped, nothing sent)");
                try { p.Kill(); p.WaitForExit(5000); } catch { }
                continue;
            }
            RECT rc; GetWindowRect(h, out rc);
            // The first-run membership card is modal and takes every key: "Not now" (0.736, 0.834 of the window) closes it.
            SetCursorPos(rc.L + (int)((rc.R - rc.L) * 0.736), rc.T + (int)((rc.B - rc.T) * 0.834)); Thread.Sleep(300);
            mouse_event(2, 0, 0, 0, IntPtr.Zero); Thread.Sleep(60); mouse_event(4, 0, 0, 0, IntPtr.Zero); Thread.Sleep(1200);
            // ...and the Welcome window that follows it: "Close" (0.540, 0.583).
            SetCursorPos(rc.L + (int)((rc.R - rc.L) * 0.540), rc.T + (int)((rc.B - rc.T) * 0.583)); Thread.Sleep(300);
            mouse_event(2, 0, 0, 0, IntPtr.Zero); Thread.Sleep(60); mouse_event(4, 0, 0, 0, IntPtr.Zero); Thread.Sleep(1200);
            SetCursorPos(rc.L + 10, rc.B - 10); // (off the model: Pick colour finds nothing)
            bool fg = GetForegroundWindow() == h;
            if (!fg) { // (a click on the way took the focus: no keys then either)
                report.AppendLine(name + ": NOT FOREGROUND (case skipped before its keys)");
                try { p.Kill(); p.WaitForExit(5000); } catch { }
                continue;
            }
            var before = Mean(Shot(h, Path.Combine(outDir, name + "-before.png")));
            if (name.StartsWith("lat")) {
                report.AppendLine(name + ": key down -> swap on screen (ms): " + Latency(name, h));
                try { p.Kill(); p.WaitForExit(5000); } catch { }
                continue;
            }
            Run(name, h);
            Thread.Sleep(1800); // (the Two-key window is 0.4 s; a waiting key runs alone after it)
            var after = Mean(Shot(h, Path.Combine(outDir, name + ".png")));
            double d = Math.Abs(after[0] - before[0]) + Math.Abs(after[1] - before[1]) + Math.Abs(after[2] - before[2]);
            report.AppendLine(string.Format("{0}: foreground={1} before=({2:F0},{3:F0},{4:F0}) after=({5:F0},{6:F0},{7:F0}) modelChanged={8}",
                name, fg, before[0], before[1], before[2], after[0], after[1], after[2], d > 12));
            try { p.Kill(); p.WaitForExit(5000); } catch { }
            if (File.Exists(keylog)) report.AppendLine("  app saw: " + File.ReadAllText(keylog).Replace("\r", "").Replace("\n", " | "));
        }
        File.WriteAllText(Path.Combine(outDir, "keydrive.txt"), report.ToString());
        Console.Write(report.ToString());
        return 0;
    }
}
