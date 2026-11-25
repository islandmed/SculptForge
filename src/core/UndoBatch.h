// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Moving along the scene's history as a whole. Every Part has its own undo stack; steps carry a number
// (UndoRecord::seq) that orders them across Parts, and the steps of one batch (an AI agent's `batch`, a checkpoint
// restore) share an id, so the undo and redo commands take them as ONE step. Checkpoints and a failed batch's rollback
// are walks along that numbering. Saving a checkpoint records its position and scene hash; restoring reuses the
// retained steps.
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

// Fingerprints Part layout, references, mesh geometry and attributes, remesh guides and the splat count.
uint64_t sceneHash(const Document& doc);

// A batch rollback only needs the cursors and scene hash. Named checkpoints also guard selection sets and splat edits.
HistoryMark markHistory(const Document& doc, bool checkpoint = true);

struct HistoryWalk {
    int undone = 0, redone = 0; // steps taken
    bool exact = false;         // the history is now exactly at the mark
    std::string problem;        // why not (cleared history, a Part gone, steps the byte budget dropped...)
};
// Undoes / redoes steps, newest first / oldest first across all Parts, until the history is at `mark`. With
// `groupId` every step it moves gets that batch id, so one undo (or redo) undoes (or redoes) the whole walk.
HistoryWalk walkToMark(Document& doc, const HistoryMark& mark, uint64_t groupId = 0);
// Check both the history position and the scene it would produce before moving the live document.
HistoryWalk restoreCheckpointMark(Document& doc, const HistoryMark& mark);

// Steps to undo and redo on the current Parts; (-1, -1) if the history was cleared or a required step was lost.
struct HistoryDistance {
    int back = 0, ahead = 0;
    bool valid = true;
};
HistoryDistance distanceToMark(const Document& doc, const HistoryMark& mark);

} // namespace sf
