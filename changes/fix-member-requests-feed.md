---
changed: Release tooling: the update feed's "N changes in this release were members' requests" number is now written automatically from the release's `member_request` notes (no manual step).
---
release.sh bump counts the fragments (assemble_notes.py --member-requests) before --apply deletes them and keeps the count
outside the checkout (a beta's carries to the stable release); the feed step writes support.member_requests_this_release,
or drops last release's when there are none. tests/release_beta_test.sh covers beta bump -> stable site.
