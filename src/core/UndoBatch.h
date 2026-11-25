// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Moving along the scene's history as a whole. Every Part has its own undo stack; steps carry a number (UndoRecord::seq)
// that orders them across Parts, and the steps of one batch (an AI agent's `batch`, a checkpoint restore) share an id,
// so the undo and redo commands take them as ONE step. Checkpoints and a failed batch's rollback are walks along that
// numbering: nothing is copied, they reuse the steps the history already keeps.
#pragma once

#include "core/Document.h"

#include <cstdint>
#include <string>

namespace sf {

// One undo / redo step on the active Part (with its Part add / remove and Figure Pose group); false when there is none.
// Does not follow a batch: the undo and redo commands do (undoStepOrBatch).
bool undoOnce(Document& doc);
bool redoOnce(Document& doc);
// undoOnce / redoOnce, then the rest of the step's batch (on whichever Parts it touched). The active Part stays active.
bool undoStepOrBatch(Document& doc);
bool redoStepOrBatch(Document& doc);

// A hash of everything a rollback must bring back: each Part's name, visibility, placement and its mesh (positions,
// triangles, mask, paint, face groups, hidden, UVs, creases, sculpt layers), and the splat count. Same scene, same hash.
uint64_t sceneHash(const Document& doc);

HistoryMark markHistory(const Document& doc); // the place the history is at now (includes sceneHash)

struct HistoryWalk {
    int undone = 0, redone = 0; // steps taken
    bool exact = false;         // the history is now exactly at the mark
    std::string problem;        // why not (cleared history, a Part gone, steps the byte budget dropped...)
};
// Undoes / redoes steps, newest first / oldest first across all Parts, until the history is at `mark`. With
// `groupId` every step it moves gets that batch id, so one undo (or redo) undoes (or redoes) the whole walk.
HistoryWalk walkToMark(Document& doc, const HistoryMark& mark, uint64_t groupId = 0);

// Why `mark` can't be reached, found WITHOUT touching the scene (empty = nothing against it): the history was cleared, or a
// step the mark points at is gone (replaced by a newer edit, or dropped by the byte budget).
std::string markUnreachable(const Document& doc, const HistoryMark& mark);

// walkToMark as a transaction (a checkpoint restore): checks markUnreachable first, and when the walk still ends off the
// mark (or the scene's hash is not `mark.hash`) walks back to where it started, so a failed restore leaves the scene,
// the history and the active Part as they were. `untouched` says so (true too when nothing was tried); on failure the
// step counts are 0. `exact` = at the mark, hash and all.
struct HistoryRestore : HistoryWalk {
    bool untouched = false;
};
HistoryRestore restoreToMark(Document& doc, const HistoryMark& mark, uint64_t groupId);

// How far the history is from `mark`: steps to undo and to redo to get there (-1, -1: the history was cleared since).
struct HistoryDistance {
    int back = 0, ahead = 0;
    bool valid = true;
};
HistoryDistance distanceToMark(const Document& doc, const HistoryMark& mark);

} // namespace sf
