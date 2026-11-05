# Show SculptForge on Discord

Let your Discord friends see that you're sculpting. Discord's profile and member list then show **SculptForge**,
its icon and how long you've been at it. It's off until you switch it on.

## Switch it on

1. Open **Edit > Preferences > Integrations**.
2. Check **Show SculptForge on Discord**.
3. Keep the Discord desktop app running on the same computer. Your status appears within a few seconds.

Two extras, both off until you check them:

- **Show the monthly challenge** adds a line "Monthly challenge: *theme*", with the theme you type in the box under
  it (64 characters at most). Handy while you work on the community's monthly challenge.
- **Show the file name** adds the name of the file you have open, for example `kraken.sfproj`. Only the name, never
  the folder it's in.

Uncheck **Show SculptForge on Discord** and your status is cleared at once. Quitting SculptForge clears it too.

## What is sent, and where

| Sent | When |
|---|---|
| The SculptForge icon and name | while it's on |
| The time you started (Discord shows the elapsed time) | while it's on |
| "Monthly challenge: *theme*" | only with **Show the monthly challenge** |
| The file's name (not its folder) | only with **Show the file name** |

Nothing else: no folders or paths, no user name, no details about your model. SculptForge passes this to the
Discord app on your own computer only, through Discord's local connection; it opens no network connection and no
port for it, and never asks Discord for anything (no sign-in, no invitations, no account access). Discord then
shows your status to your friends and to the servers you share, as it does for games.

> **Note:** Discord shows your status only if **Display current activity as a status message** is on in Discord's
> **User Settings > Activity Privacy**.

## If it doesn't show

- Check that the Discord desktop app is running and you're signed in. The website version of Discord can't show it.
- Discord in a Flatpak or Snap works; a Discord running as another user (or as administrator on Windows while
  SculptForge isn't) is ignored on purpose.
- Started Discord after SculptForge? It connects on its own within a minute.

## See also

- [The interface](interface.md)
- [Troubleshooting](troubleshooting.md)
