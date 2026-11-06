---
whats_new: **Studio controls**: IT can deploy a policy file that locks AI, network features, AI providers, AI tools and
  updates (**Managed by your studio** shows on locked settings), keep an audit log of every AI action, and sign artists
  in with the studio account (the guide's "SculptForge for studios" page, IT deployment).
whats_new: **AI providers**: **Edit > Preferences > AI > Provider** has presets for Anthropic, OpenAI, Ollama, LM Studio,
  vLLM, llama.cpp, a LiteLLM studio gateway and OpenRouter. Keys now live in your system keychain.
highlight: Studio-ready AI: policy, audit log, single sign-on
added: **Managed policy for studios** (docs/guide/studios.md "IT deployment"): `policy.json` in `/etc/sculptforge`,
  `/Library/Application Support/SculptForge` or `C:\ProgramData\SculptForge` (or `SF_POLICY` when there is none) locks
  AI on or off, network features, the allowed AI providers and addresses, sign-in for AI, AI tools (MCP on or off, never
  controlling the window, allowed kinds of command), the update check, channel and feed. Locked settings say **Managed by
  your studio**; a broken policy fails closed. `sculptforge-cli --policy` shows what it manages.
added: **AI audit log**: every AI tool (MCP) command, refused ones too, and every Brush Lab and Generate 3D request is a
  JSON line (time, user, host, client, command, a summary of its settings, result) in a local file or a share the policy
  names, rotated by size. No artwork, keys or long text. `sculptforge-cli --audit-log` prints the path.
added: **Studio sign-in** (Okta, Microsoft Entra ID, Google Workspace, Auth0, Keycloak): OpenID Connect device sign-in in
  the artist's browser, no local listener; tokens in the keychain, sent only to the studio's own AI gateway.
  **Edit > Preferences > AI > Studio sign-in**, or `sculptforge-cli --sign-in`.
added: **AI provider presets** in **Edit > Preferences > AI > Provider** (also in Brush Lab): Anthropic, OpenAI, Ollama,
  LM Studio, vLLM, llama.cpp server, LiteLLM proxy (a studio gateway for Bedrock, Vertex AI and Azure OpenAI) and
  OpenRouter.
added: Each release carries a CycloneDX software bill of materials (`SculptForge-<version>-sbom.cdx.json`), covered by the
  signed checksums. A one-page security overview for IT reviews is in the guide.
security: AI keys (Brush Lab, Hugging Face, fal.ai, Meshy) are stored in the operating system's keychain (macOS Keychain,
  Windows Credential Manager, Linux Secret Service) instead of `ai.json`; keys already in `ai.json` move there at the
  first start. Without a keychain (`SF_KEYCHAIN=none`), a key lasts for the session; environment variables still work.
testers:
  - Any OS: Edit > Preferences > AI > Provider: pick each preset; the endpoint and note change; save; restart; the
    choice stays and the key is still used (and ai.json has no key in it).
  - Linux: write /etc/sculptforge/policy.json with "ai": {"enabled": false}; start: Enable AI features is greyed with
    "Managed by your studio". Break the file (a typo in a key): AI, AI tools and network features are off.
  - Any OS: run an AI tool against `sculptforge-cli --mcp`; each command appears in the audit log
    (`sculptforge-cli --audit-log`).
---
Policy: src/core/ManagedPolicy.*; audit: src/core/AuditLog.*; presets ai/Providers.*; keys ai/Secrets.* and
core/Keychain.cpp (macOS Keychain added); sign-in ai/Oidc.* (mock IdP tests in tests/test_studio_policy.cpp).
