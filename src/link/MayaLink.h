// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Maya Live link (#42; File > Maya > Live link, opt-in, off at every start): the SculptForge shelf script for
// Maya connects to the open app for live updates, so Maya follows a stroke while it is drawn. On top of the
// exchange folder (io/MayaBridge.h), never instead of it.
//
// Where: the same kind of local endpoint as live mode (link/AiLink.h), never a TCP or UDP port: a Unix domain
// socket (0600, in a folder only this user can enter; every peer's user is checked) or a named pipe whose DACL
// grants this user only, remote clients rejected. Browsers can't open either, other users and the network can't
// reach them. On top of that, a random pairing code shown in SculptForge must open every connection, and one
// client at a time.
//
// What: mesh data only. No commands, paths, scripts or anything that is run. Little-endian frames:
//   header (16 bytes): "SFML", u16 version (1), u16 type, u64 payload length (<= kMayaLinkMaxPayload)
//   Hello   (client first, within kMayaLinkHelloMs): 32 ASCII hex pairing code, u16 n, n bytes client name
//   Welcome (app's answer):                         u16 n, n bytes app version
//   Mesh:    u16 n, name (Maya characters), u32 V, u32 F, u32 C, u8 flags (1 = UVs), f32 x3 V positions,
//            u8 F face sizes (3..255, summing to C), u32 C vertex indices (< V), [f32 x2 C UVs per corner]
//   Points:  u16 n, name, u32 V (the mesh's vertex count), u32 N, u32 N indices (< V), f32 x3 N positions
//   Bye:     empty
// Everything is checked before use: sizes add up exactly, indices are in range, numbers are finite, names are
// Maya names. Anything else closes the connection.
#pragma once

#include "core/Mesh.h"
#include "link/AiLink.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sf::link {

inline constexpr uint16_t kMayaLinkVersion = 1;
inline constexpr size_t kMayaLinkHeaderBytes = 16;
inline constexpr uint64_t kMayaLinkMaxPayload = 1ull << 30;     // 1 GiB: a 20M-triangle mesh with UVs fits
inline constexpr uint32_t kMayaLinkMaxVertices = 64u << 20;
inline constexpr size_t kMayaLinkCodeChars = 32;
inline constexpr int kMayaLinkHelloMs = 5000;                    // to send Hello after connecting
inline constexpr int kMayaLinkFrameMs = 30000;                   // no byte of a started frame for this long: closed
inline constexpr uint64_t kMayaLinkMaxHello = 256;               // before pairing, no frame may claim more
inline constexpr size_t kMayaLinkReadChunk = 1u << 20;           // payloads grow as bytes arrive, a MiB at a time
inline constexpr unsigned long long kMayaLinkStreamNs = 100'000'000ull; // mid-stroke Points, at most this often

enum class MayaLinkType : uint16_t { Hello = 1, Welcome = 2, Mesh = 3, Points = 4, Bye = 5 };

struct MayaLinkMesh {
    std::string name;
    std::vector<float> positions;   // 3 per vertex
    std::vector<uint8_t> faceSizes; // per face
    std::vector<uint32_t> indices;  // per corner
    std::vector<float> uvs;         // 2 per corner, or empty
};

struct MayaLinkPoints {
    std::string name;
    uint32_t vertexCount = 0;
    std::vector<uint32_t> indices;
    std::vector<float> positions; // 3 per index
};

// A new pairing code: 32 hex digits (128 random bits).
std::string mayaLinkNewCode();
// The code as shown and pasted: "XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX" (upper case). Pasting takes either form.
std::string mayaLinkShowCode(const std::string& code);
// What Copy puts on the clipboard for the shelf's Link button: "sflink1 <code> <endpoint>".
std::string mayaLinkPairingText(const std::string& code, const std::string& endpoint);
// Constant-time comparison of a sent code (any case, dashes ignored) with the app's.
bool mayaLinkCodeMatches(const std::string& sent, const std::string& code);

// Frames. encode* give a whole frame (header + payload); parse* check a payload of that type strictly.
std::string mayaLinkFrame(MayaLinkType type, const std::string& payload);
// The header: false (with `error`) for a wrong magic, version, unknown type or a payload over the cap.
bool parseMayaLinkHeader(const char* header, MayaLinkType& type, uint64_t& length, std::string& error);
std::string encodeMayaLinkHello(const std::string& code, const std::string& client);
bool parseMayaLinkHello(const std::string& payload, std::string& code, std::string& client, std::string& error);
std::string encodeMayaLinkWelcome(const std::string& version);
std::string encodeMayaLinkMesh(const MayaLinkMesh& mesh);
bool parseMayaLinkMesh(const std::string& payload, MayaLinkMesh& out, std::string& error);
std::string encodeMayaLinkPoints(const MayaLinkPoints& points);
bool parseMayaLinkPoints(const std::string& payload, MayaLinkPoints& out, std::string& error);

// SculptForge's meshes as link meshes (quads as quads, UVs per corner) and back (polygons fanned into
// triangles, quads kept as pairs, normals and topology rebuilt).
MayaLinkMesh mayaLinkMeshFrom(const std::string& name, const Mesh& mesh);
bool mayaLinkMeshTo(const MayaLinkMesh& in, Mesh& out, std::string& error);
// The vertices of `now` that moved from `before` (same vertex count), as Points. Empty when none moved.
MayaLinkPoints mayaLinkPointsBetween(const std::string& name, const std::vector<glm::vec3>& before,
                                     const std::vector<glm::vec3>& now);

// Merges `later` into `into` (same object): the union of their vertices, `later`'s positions winning. Points
// carry absolute positions, so queued updates fold into one and a slow reader never falls behind.
void mergeMayaLinkPoints(MayaLinkPoints& into, const MayaLinkPoints& later);

// The app's end: a worker thread that listens, pairs and carries frames. Received meshes queue up for the main
// thread (takeReceived); sends queue up for the worker, which encodes them (Points for an object fold into the
// queued ones; a Mesh drops the Points queued before it). One client at a time.
class MayaLinkServer {
public:
    ~MayaLinkServer() { stop(); }
    // Starts listening on `endpoint` (localEndpoint("maya-link")) with a new pairing code.
    bool start(const std::string& endpoint, std::string& error);
    void stop();
    bool running() const { return running_; }
    bool connected() const { return connected_; }
    const std::string& code() const { return code_; }
    const std::string& endpoint() const { return endpoint_; }
    std::string clientName() const;
    // Counts for the UI and tests: refused connections (wrong code, a second client, bad frames).
    int refused() const { return refused_; }
    // Tests: the biggest payload buffer a read ever held (claims never allocate ahead of the bytes), and a shorter
    // stall limit than kMayaLinkFrameMs.
    size_t largestBuffer() const { return largestBuffer_; }
    void setFrameTimeoutMs(int ms) { frameMs_ = ms; }
    void send(MayaLinkMesh mesh);
    void send(MayaLinkPoints points);
    std::vector<MayaLinkMesh> takeReceived();
    std::string takeEvent(); // "connected", "disconnected: ...", "refused: ..." (one at a time, "" = none)

private:
    void run();
    bool serve(AiLinkConn& conn);
    bool readFrame(AiLinkConn& conn, MayaLinkType& type, std::string& payload, int firstByteMs, uint64_t maxLength,
                   std::string& error);
    bool flush(AiLinkConn& conn);
    void event(std::string text);

    AiLinkListener listener_;
    std::thread thread_;
    std::atomic<bool> running_{false}, stopping_{false}, connected_{false};
    std::atomic<int> refused_{0};
    std::atomic<size_t> largestBuffer_{0};
    std::atomic<int> frameMs_{kMayaLinkFrameMs};
    std::string code_, endpoint_;
    mutable std::mutex mutex_;
    std::string client_;
    struct Outgoing {
        bool isMesh = false;
        MayaLinkMesh mesh;
        MayaLinkPoints points;
    };
    std::deque<Outgoing> outbox_;
    std::vector<MayaLinkMesh> inbox_;
    std::deque<std::string> events_;
};

} // namespace sf::link
