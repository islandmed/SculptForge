// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The user guide (docs/guide/*.md): a parser for the Markdown subset the guide
// uses, the book's table of contents (index.md) and full-text search. No UI:
// the in-app reader (app/UserGuide.cpp), the website generator's checks and
// the unit tests share it.
//
// The subset: one "# Title", "##"/"###" headings, paragraphs with **bold**,
// *italic*, `code`, <kbd>keys</kbd> and [links](target), "- " and "1. " lists
// (one nesting level), pipe tables, ``` code blocks, "> **Label:** ..."
// callouts (a "> - item" line adds a bullet to one), images on their own line and "---" rules. "<!-- -->" lines are
// comments. The guide's own blocks: a ```cards fence (one "[Title](target) | images/x.jpg | blurb" line per card),
// "<details>" / "<summary>Label</summary>" / "</details>" lines around folded blocks, and the first image before any
// "##" heading is the chapter's hero. Bold text with " > " in it (**Edit > Preferences > Input**) is a menu path.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sf::guide {

struct Span {
    std::string text;
    bool bold = false, italic = false, code = false, kbd = false;
    bool path = false;  // a menu path (bold "Edit > Preferences"), drawn as chips
    std::string link; // target when the span is (part of) a link
};
using Spans = std::vector<Span>;

enum class BlockKind { Heading, Paragraph, Bullet, Numbered, Code, Table, Image, Callout, Rule, Cards, Details, DetailsEnd };

struct Card {
    std::string title, target, image;
    Spans blurb;
};

struct Block {
    BlockKind kind = BlockKind::Paragraph;
    int level = 0;       // heading level (1-3); list nesting (0-1)
    int number = 0;      // numbered list item
    Spans spans;         // heading, paragraph, list item, callout
    std::string text;    // code block contents; image source
    std::string alt;     // image alt text (shown as its caption)
    std::string lang;    // code block language
    std::string anchor;  // heading slug ("mask-by-cavity")
    std::vector<std::vector<Spans>> rows; // table: rows of cells, row 0 is the header
    std::vector<Spans> items; // callout: its bullets
    std::vector<Card> cards;  // card grid
    bool hero = false;        // image: the chapter's opening picture
    int end = -1;             // details: the index of its DetailsEnd block
};

struct Document {
    std::string title; // the "# Title" text
    std::vector<Block> blocks;
};

Document parse(std::string_view markdown);
Spans parseInline(std::string_view text);
// GitHub-style heading anchor: lowercase, punctuation dropped, spaces to '-'.
std::string slugify(std::string_view heading);
std::string plainText(const Spans& spans);
std::string plainText(const Block& block); // every visible word of a block (tables: all cells)

// Every link target and image source in a document, in order.
struct Reference {
    std::string target;
    bool image = false;
};
std::vector<Reference> references(const Document& doc);

// "masking.md#mask-by-cavity" -> {"masking.md", "mask-by-cavity"}; "#x" -> {"", "x"}.
struct Target {
    std::string file, anchor;
    bool external = false; // http(s)://, mailto:
};
Target splitTarget(std::string_view target);

// ---------------------------------------------------------------------------

struct Chapter {
    std::string file;  // "masking.md"
    std::string title; // from its "# Title"
    std::string group; // the index.md section it is listed under ("Sculpting")
    Document doc;
};

struct SearchHit {
    int chapter = -1;
    int block = -1;        // the block that matched
    std::string section;   // nearest heading above it ("" = chapter start)
    std::string anchor;    // that heading's anchor
    std::string snippet;   // text around the first match
    size_t matchStart = 0, matchLength = 0; // the first query word inside `snippet`
    int score = 0;
};

class Book {
public:
    // index.md lists the chapters ("1. [Title](file.md)") under "##" group headings; `load` is asked
    // for each file's text and returns false when it is missing.
    using Loader = bool (*)(const std::string& file, std::string& text, void* user);
    bool load(Loader loader, void* user, std::string& error);

    const Document& index() const { return index_; }
    const std::vector<Chapter>& chapters() const { return chapters_; }
    int find(std::string_view file) const; // chapter index, -1 if unknown ("index.md" is not a chapter)

    // Every query word (case-insensitive) must appear in one block; headings and titles score higher.
    // One hit per section (its first matching text block), at most `limit`, best first, then in reading order.
    std::vector<SearchHit> search(std::string_view query, size_t limit = 50) const;

private:
    Document index_;
    std::vector<Chapter> chapters_;
};

} // namespace sf::guide
