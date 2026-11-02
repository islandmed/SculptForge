**Status: STOPPED (usage cap), NOT pushed.** The code is finished and committed on feat/discord-intake. The full ctest has never completed: the first run was paused for the benchmark, and the second was stopped mid-build. Targeted checks pass: the `discord_intake` test (8 cases), yamllint, actionlint and ruff.
**Left:** a green full ctest, then push, then message the coordinator. **To resume** (check first that `pgrep -f ctest` finds nothing):
`cd .claude/worktrees/discord-intake && nice -n 19 ionice -c3 cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && nice -n 19 ionice -c3 cmake --build build -j3 && (cd build && SDL_APP_ID=sculptforge-agent nice -n 19 ionice -c3 ctest -j3 --output-on-failure) && git push -u origin feat/discord-intake`

**discord-intake** (feat/discord-intake). `tools/discord_intake.py` and `.github/workflows/discord-intake.yml` (runs every 30 min plus manual runs; issues write, contents read). Each new post becomes an issue labelled from-discord plus idea, help or bug, with a "Tracked as #N" reply. When an issue closes, the owner's last comment goes back to Discord once. It dedupes using a marker in each issue, files at most 10 issues per run, skips posts under 20 characters and anything from bots or AutoMod, and turns attachments into links without downloading them. It does nothing until DISCORD_BOT_TOKEN is set.

**Tony's 5-minute setup:**
1. In discord.com/developers/applications, click New Application ("SculptForge Intake"). On the Bot tab: turn Public Bot off, turn on Message Content Intent (leave the other intents off), then Reset Token and copy it.
2. Open `https://discord.com/oauth2/authorize?client_id=<APPLICATION_ID>&scope=bot&permissions=66560` (View Channels + Read Message History only) and add the bot to SculptForge.
3. Go to Server Settings > Roles > SculptForge Intake and turn off View Channels and Read Message History. Then in #ideas, #help and #bug-reports, open Edit Channel > Permissions, add that role, and allow View Channel and Read Message History.
4. Run `gh secret set DISCORD_BOT_TOKEN -R islandmed/SculptForge` and paste the token. Optional: create webhooks for #help and #bug-reports (Edit Channel > Integrations > Webhooks), then `gh secret set DISCORD_HELP_WEBHOOK` and `gh secret set DISCORD_BUGS_WEBHOOK`. DISCORD_IDEAS_WEBHOOK is already set.
5. Test it: `gh workflow run discord-intake.yml && gh run watch`.
