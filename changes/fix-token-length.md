---
fixed: **Member builds sign-in** accepts GitHub's longer tokens (its new ~520-character format, and up to 4096
  characters): before, a long token was refused and the sign-in failed without a word. On Windows, where one Credential
  Manager entry holds at most 2560 bytes, a longer sign-in is kept across several entries (studio sign-ins too);
  sign-ins stored by earlier versions still load.
testers:
  - Windows: in the update panel's Member builds section, Sign in with GitHub; restart; still signed in. Sign out; Credential Manager shows no
    SculptForge member-builds entries left.
---
Device codes get the same 4096 cap. HTTP headers were already unbounded std::string (curl_slist / WinHTTP); no
workflow assumes a token length (grep of .github/workflows).
