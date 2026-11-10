---
fixed: Generate 3D: when a free Hugging Face model finishes without a model, or refuses the job, the panel now says so in red under Generate, with the service name (it used to show only in the status bar, so it looked silent). Text to 3D on the two free Hunyuan3D Spaces is switched off by their owners; the message says that and points to Image to 3D or another provider. Photos from the phone use the same message.
---
Replies that hold the model as a bare link or a list are now found too. scripts/hf-space-health.sh (daily systemd timer, no Claude) reports when a free Space goes down or changes its inputs.
