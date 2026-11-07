# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   python3 -I tests/maya/link_peer.py "sflink1 CODE ENDPOINT"
#
# The Maya shelf script's Live link client without Maya, for tests/test_maya_live.cpp (maya_link_python_client):
# pairs with the app's server through the real endpoint (Unix socket, or on Windows the named pipe), sends one mesh
# as Maya would, then waits for the Points the test streams back and checks they fold. Exit 0 = all well.
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "integrations", "maya"))
import sculptforge_maya as sm  # noqa: E402

CUBE = {"name": "pCube1",
        "positions": [-1, -1, 1, 1, -1, 1, -1, 1, 1, 1, 1, 1, -1, 1, -1, 1, 1, -1, -1, -1, -1, 1, -1, -1],
        "face_sizes": [4, 4, 4, 4, 4, 4],
        "indices": [0, 1, 3, 2, 2, 3, 5, 4, 4, 5, 7, 6, 6, 7, 1, 0, 1, 7, 5, 3, 6, 0, 2, 4], "uvs": []}


def main():
    code, endpoint = sm.parse_pairing(sys.argv[1])
    conn = sm._PipeConn(endpoint) if os.name == "nt" else sm._SocketConn(endpoint)
    inbox = sm.LinkInbox()
    client = sm.LinkClient(conn, code, inbox.add, client="link_peer")
    client.send(sm.encode_mesh(CUBE))
    moved = {}
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and len(moved) < 4:
        for name, mesh, points in inbox.take():
            if name == "pCube1" and points is not None and points[0] == 8:
                moved.update(points[1])
        time.sleep(0.02)
    client.close()
    ok = sorted(moved) == [0, 1, 2, 7] and moved[7] == (9.0, 9.0, 2.0) and moved[1] == (1.0, 0.0, 0.0)
    print("link_peer: %s (%s)" % ("ok" if ok else "FAILED", sorted(moved.items())))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
