// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Guide.h"

#include <algorithm>
#include <cctype>

namespace sf::guide {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

size_t indentOf(std::string_view line) {
    size_t n = 0;
    for (char c : line) {
        if (c == ' ') ++n;
        else if (c == '\t') n += 4;
        else break;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Inline formatting
// ---------------------------------------------------------------------------

struct InlineParser {
    Spans out;
    std::string buf;
    bool bold = false, italic = false;
    std::string link;

    void flush() {
        if (buf.empty()) return;
        Span s;
        s.text = std::move(buf);
        s.bold = bold;
        s.italic = italic;
        s.link = link;
        out.push_back(std::move(s));
        buf.clear();
    }
    void push(Span s) {
        flush();
        s.link = link;
        out.push_back(std::move(s));
    }

    void run(std::string_view t) {
        for (size_t i = 0; i < t.size();) {
            const char c = t[i];
            if (c == '\\' && i + 1 < t.size() && std::ispunct(static_cast<unsigned char>(t[i + 1]))) {
                buf += t[i + 1];
                i += 2;
                continue;
            }
            if (c == '`') {
                const size_t close = t.find('`', i + 1);
                if (close != std::string_view::npos) {
                    Span s;
                    s.text = std::string(t.substr(i + 1, close - i - 1));
                    s.code = true;
                    push(std::move(s));
                    i = close + 1;
                    continue;
                }
            }
            if (startsWith(t.substr(i), "<kbd>")) {
                const size_t close = t.find("</kbd>", i + 5);
                if (close != std::string_view::npos) {
                    Span s;
                    s.text = std::string(t.substr(i + 5, close - i - 5));
                    s.kbd = true;
                    push(std::move(s));
                    i = close + 6;
                    continue;
                }
            }
            if (c == '<' && (startsWith(t.substr(i + 1), "http://") || startsWith(t.substr(i + 1), "https://"))) {
                const size_t close = t.find('>', i + 1);
                if (close != std::string_view::npos) {
                    flush();
                    const std::string url(t.substr(i + 1, close - i - 1));
                    const std::string saved = link;
                    link = url;
                    buf = url;
                    flush();
                    link = saved;
                    i = close + 1;
                    continue;
                }
            }
            if (c == '&') {
                static const std::pair<const char*, const char*> kEntities[] = {
                    {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&nbsp;", "\xC2\xA0"}};
                bool done = false;
                for (const auto& [name, value] : kEntities)
                    if (startsWith(t.substr(i), name)) {
                        buf += value;
                        i += std::char_traits<char>::length(name);
                        done = true;
                        break;
                    }
                if (done) continue;
            }
            if (c == '*' && i + 1 < t.size() && t[i + 1] == '*') {
                flush();
                bold = !bold;
                i += 2;
                continue;
            }
            if (c == '*') {
                // Emphasis only where it reads as such: "*word" opens, "word*" closes; "5 * 3" stays.
                const bool canOpen = !italic && i + 1 < t.size() && !isSpace(t[i + 1]) &&
                                     t.find('*', i + 1) != std::string_view::npos;
                const bool canClose = italic && i > 0 && !isSpace(t[i - 1]);
                if (canOpen || canClose) {
                    flush();
                    italic = !italic;
                    ++i;
                    continue;
                }
            }
            if (c == '[' && link.empty()) {
                // [text](target): find the matching ']' (nested brackets allowed), then "(...)".
                int depth = 0;
                size_t close = std::string_view::npos;
                for (size_t k = i; k < t.size(); ++k) {
                    if (t[k] == '`') { // brackets inside code don't count
                        const size_t e = t.find('`', k + 1);
                        if (e == std::string_view::npos) break;
                        k = e;
                        continue;
                    }
                    if (t[k] == '[') ++depth;
                    else if (t[k] == ']' && --depth == 0) {
                        close = k;
                        break;
                    }
                }
                if (close != std::string_view::npos && close + 1 < t.size() && t[close + 1] == '(') {
                    const size_t end = t.find(')', close + 2);
                    if (end != std::string_view::npos) {
                        flush();
                        link = std::string(trim(t.substr(close + 2, end - close - 2)));
                        run(t.substr(i + 1, close - i - 1));
                        flush();
                        link.clear();
                        i = end + 1;
                        continue;
                    }
                }
            }
            buf += c;
            ++i;
        }
    }
};

Spans merged(Spans in) {
    Spans out;
    for (auto& s : in) {
        if (s.text.empty()) continue;
        if (!out.empty()) {
            Span& p = out.back();
            if (p.bold == s.bold && p.italic == s.italic && p.code == s.code && p.kbd == s.kbd && p.link == s.link && !s.code &&
                !s.kbd) {
                p.text += s.text;
                continue;
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

bool isRule(std::string_view t) {
    if (t.size() < 3) return false;
    for (char c : t)
        if (c != '-' && c != ' ') return false;
    return std::count(t.begin(), t.end(), '-') >= 3;
}

// "- item" / "* item" -> true, the text after the marker.
bool bulletItem(std::string_view t, std::string_view& rest) {
    if (t.size() >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') && t[1] == ' ') {
        rest = trim(t.substr(2));
        return true;
    }
    return false;
}

bool numberedItem(std::string_view t, int& number, std::string_view& rest) {
    size_t k = 0;
    while (k < t.size() && std::isdigit(static_cast<unsigned char>(t[k]))) ++k;
    if (k == 0 || k > 4 || k + 1 >= t.size() || (t[k] != '.' && t[k] != ')') || t[k + 1] != ' ') return false;
    number = std::stoi(std::string(t.substr(0, k)));
    rest = trim(t.substr(k + 2));
    return true;
}

bool imageLine(std::string_view t, std::string& alt, std::string& src) {
    if (!startsWith(t, "![") || t.back() != ')') return false;
    const size_t mid = t.find("](");
    if (mid == std::string_view::npos) return false;
    alt = std::string(t.substr(2, mid - 2));
    src = std::string(trim(t.substr(mid + 2, t.size() - mid - 3)));
    return true;
}

std::vector<std::string_view> tableCells(std::string_view line) {
    line = trim(line);
    if (!line.empty() && line.front() == '|') line.remove_prefix(1);
    if (!line.empty() && line.back() == '|') line.remove_suffix(1);
    std::vector<std::string_view> cells;
    size_t start = 0;
    bool inCode = false;
    for (size_t k = 0; k <= line.size(); ++k) {
        if (k < line.size() && !inCode && startsWith(line.substr(k), "<kbd>")) { // <kbd>`</kbd>, <kbd>|</kbd>
            const size_t close = line.find("</kbd>", k);
            if (close != std::string_view::npos) {
                k = close + 5;
                continue;
            }
        }
        if (k < line.size() && line[k] == '`') inCode = !inCode;
        if (k == line.size() || (line[k] == '|' && !inCode && (k == 0 || line[k - 1] != '\\'))) {
            cells.push_back(trim(line.substr(start, k - start)));
            start = k + 1;
        }
    }
    return cells;
}

bool isTableSeparator(std::string_view line) {
    const auto cells = tableCells(line);
    if (cells.empty()) return false;
    for (auto c : cells) {
        if (c.empty()) return false;
        for (char ch : c)
            if (ch != '-' && ch != ':' && ch != ' ') return false;
    }
    return true;
}

} // namespace

Spans parseInline(std::string_view text) {
    InlineParser p;
    p.run(text);
    p.flush();
    Spans out = merged(std::move(p.out));
    for (auto& s : out) s.path = s.bold && !s.code && !s.kbd && s.link.empty() && s.text.find(" > ") != std::string::npos;
    return out;
}

std::string slugify(std::string_view heading) {
    std::string out;
    for (char c : heading) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) out += char(std::tolower(u));
        else if (c == ' ') out += '-';
        else if (c == '-' || c == '_' || u >= 0x80) out += c;
    }
    return out;
}

std::string plainText(const Spans& spans) {
    std::string out;
    for (const auto& s : spans) out += s.text;
    return out;
}

std::string plainText(const Block& block) {
    switch (block.kind) {
    case BlockKind::Code: return block.text;
    case BlockKind::Image: return block.alt;
    case BlockKind::Callout: {
        std::string out = plainText(block.spans);
        for (const auto& item : block.items) out += ' ' + plainText(item);
        return out;
    }
    case BlockKind::Cards: {
        std::string out;
        for (const auto& c : block.cards) out += (out.empty() ? "" : " ") + c.title + ' ' + plainText(c.blurb);
        return out;
    }
    case BlockKind::Table: {
        std::string out;
        for (const auto& row : block.rows)
            for (const auto& cell : row) {
                if (!out.empty()) out += ' ';
                out += plainText(cell);
            }
        return out;
    }
    default: return plainText(block.spans);
    }
}

Document parse(std::string_view md) {
    Document doc;
    std::vector<std::string_view> lines;
    for (size_t start = 0; start <= md.size();) {
        size_t end = md.find('\n', start);
        if (end == std::string_view::npos) end = md.size();
        std::string_view line = md.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
    }

    std::vector<std::string> usedAnchors;
    std::vector<size_t> openDetails; // <details> blocks waiting for their </details>
    std::string para;          // paragraph text being collected
    Block* listItem = nullptr; // the list item that continuation lines join
    auto endParagraph = [&] {
        if (!para.empty()) {
            Block b;
            b.kind = BlockKind::Paragraph;
            b.spans = parseInline(para);
            doc.blocks.push_back(std::move(b));
            para.clear();
        }
    };
    auto endList = [&] {
        if (listItem) listItem->spans = parseInline(listItem->text), listItem->text.clear();
        listItem = nullptr;
    };

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view raw = lines[i];
        const std::string_view t = trim(raw);
        if (t.empty()) {
            endParagraph();
            endList();
            continue;
        }
        if (startsWith(t, "<!--")) { // comments (generated-section markers)
            endParagraph();
            endList();
            size_t j = i;
            while (j < lines.size() && lines[j].find("-->") == std::string_view::npos) ++j;
            i = j;
            continue;
        }
        if (startsWith(t, "```cards")) { // [Title](target) | images/x.jpg | blurb
            endParagraph();
            endList();
            Block b;
            b.kind = BlockKind::Cards;
            for (++i; i < lines.size() && !startsWith(trim(lines[i]), "```"); ++i) {
                const auto cells = tableCells(lines[i]);
                if (cells.empty() || cells[0].empty()) continue;
                Card c;
                for (const auto& sp : parseInline(cells[0])) c.title += sp.text, c.target = sp.link.empty() ? c.target : sp.link;
                if (cells.size() > 1) c.image = std::string(cells[1]);
                if (cells.size() > 2) c.blurb = parseInline(cells[2]);
                b.cards.push_back(std::move(c));
            }
            doc.blocks.push_back(std::move(b));
            continue;
        }
        if (t == "<details>") {
            endParagraph();
            endList();
            Block b;
            b.kind = BlockKind::Details;
            if (i + 1 < lines.size() && startsWith(trim(lines[i + 1]), "<summary>")) {
                std::string_view sum = trim(lines[++i]).substr(9);
                if (const size_t close = sum.find("</summary>"); close != std::string_view::npos) sum = sum.substr(0, close);
                b.spans = parseInline(trim(sum));
            }
            openDetails.push_back(doc.blocks.size());
            doc.blocks.push_back(std::move(b));
            continue;
        }
        if (t == "</details>") {
            endParagraph();
            endList();
            if (!openDetails.empty()) {
                doc.blocks[openDetails.back()].end = int(doc.blocks.size());
                openDetails.pop_back();
                Block b;
                b.kind = BlockKind::DetailsEnd;
                doc.blocks.push_back(std::move(b));
            }
            continue;
        }
        if (startsWith(t, "```")) {
            endParagraph();
            endList();
            Block b;
            b.kind = BlockKind::Code;
            b.lang = std::string(trim(t.substr(3)));
            const size_t indent = indentOf(raw);
            for (++i; i < lines.size() && !startsWith(trim(lines[i]), "```"); ++i) {
                std::string_view l = lines[i];
                const size_t drop = std::min(indent, indentOf(l));
                b.text += std::string(l.substr(std::min(drop, l.size())));
                b.text += '\n';
            }
            if (!b.text.empty()) b.text.pop_back();
            doc.blocks.push_back(std::move(b));
            continue;
        }
        if (t[0] == '#') {
            size_t level = 0;
            while (level < t.size() && t[level] == '#') ++level;
            if (level <= 6 && level < t.size() && t[level] == ' ') {
                endParagraph();
                endList();
                Block b;
                b.kind = BlockKind::Heading;
                b.level = int(std::min<size_t>(level, 3));
                std::string_view text = trim(t.substr(level));
                b.spans = parseInline(text);
                std::string anchor = slugify(plainText(b.spans));
                const std::string base = anchor;
                for (int n = 1; std::find(usedAnchors.begin(), usedAnchors.end(), anchor) != usedAnchors.end(); ++n)
                    anchor = base + "-" + std::to_string(n);
                usedAnchors.push_back(anchor);
                b.anchor = anchor;
                if (level == 1 && doc.title.empty()) doc.title = plainText(b.spans);
                doc.blocks.push_back(std::move(b));
                continue;
            }
        }
        if (t[0] == '|' && i + 1 < lines.size() && isTableSeparator(lines[i + 1])) {
            endParagraph();
            endList();
            Block b;
            b.kind = BlockKind::Table;
            auto addRow = [&](std::string_view line) {
                std::vector<Spans> row;
                for (auto cell : tableCells(line)) row.push_back(parseInline(cell));
                b.rows.push_back(std::move(row));
            };
            addRow(t);
            for (i += 2; i < lines.size() && !trim(lines[i]).empty() && trim(lines[i])[0] == '|'; ++i) addRow(lines[i]);
            --i;
            const size_t columns = b.rows.front().size();
            for (auto& row : b.rows) row.resize(columns);
            doc.blocks.push_back(std::move(b));
            continue;
        }
        if (t[0] == '>') {
            endParagraph();
            endList();
            std::string text;
            std::vector<std::string> items; // "> - item" lines (a wrapped item continues on the next "> " line)
            for (; i < lines.size() && !trim(lines[i]).empty() && trim(lines[i])[0] == '>'; ++i) {
                const std::string_view line = trim(trim(lines[i]).substr(1));
                std::string_view rest;
                if (bulletItem(line, rest)) items.emplace_back(rest);
                else if (!items.empty()) items.back() += ' ' + std::string(line);
                else text += (text.empty() ? "" : " ") + std::string(line);
            }
            --i;
            Block b;
            b.kind = BlockKind::Callout;
            b.spans = parseInline(text);
            for (const auto& item : items) b.items.push_back(parseInline(item));
            doc.blocks.push_back(std::move(b));
            continue;
        }
        std::string alt, src;
        if (imageLine(t, alt, src)) {
            endParagraph();
            endList();
            Block b;
            b.kind = BlockKind::Image;
            b.alt = alt;
            b.text = src;
            doc.blocks.push_back(std::move(b));
            continue;
        }
        if (isRule(t) && para.empty()) {
            endList();
            Block b;
            b.kind = BlockKind::Rule;
            doc.blocks.push_back(std::move(b));
            continue;
        }
        std::string_view rest;
        int number = 0;
        const bool bullet = bulletItem(t, rest);
        const bool numbered = !bullet && numberedItem(t, number, rest);
        if (bullet || numbered) {
            endParagraph();
            endList();
            Block b;
            b.kind = bullet ? BlockKind::Bullet : BlockKind::Numbered;
            b.number = number;
            b.level = indentOf(raw) >= 2 ? 1 : 0;
            b.text = std::string(rest);
            doc.blocks.push_back(std::move(b));
            listItem = &doc.blocks.back();
            continue;
        }
        if (listItem) { // a wrapped list item continues
            listItem->text += ' ';
            listItem->text += std::string(t);
            continue;
        }
        if (!para.empty()) para += ' ';
        para += std::string(t);
    }
    endParagraph();
    endList();
    for (size_t k : openDetails) doc.blocks[k].end = int(doc.blocks.size()); // an unclosed <details> runs to the end
    for (auto& b : doc.blocks) { // the hero: the first image before any "##" heading
        if (b.kind == BlockKind::Heading && b.level >= 2) break;
        if (b.kind == BlockKind::Image) {
            b.hero = true;
            break;
        }
    }
    return doc;
}

std::vector<Reference> references(const Document& doc) {
    std::vector<Reference> out;
    auto fromSpans = [&](const Spans& spans) {
        std::string last;
        for (const auto& s : spans)
            if (!s.link.empty() && s.link != last) {
                out.push_back({s.link, false});
                last = s.link;
            } else if (s.link.empty()) {
                last.clear();
            }
    };
    for (const auto& b : doc.blocks) {
        if (b.kind == BlockKind::Image) out.push_back({b.text, true});
        fromSpans(b.spans);
        for (const auto& item : b.items) fromSpans(item);
        for (const auto& c : b.cards) {
            if (!c.target.empty()) out.push_back({c.target, false});
            if (!c.image.empty()) out.push_back({c.image, true});
            fromSpans(c.blurb);
        }
        for (const auto& row : b.rows)
            for (const auto& cell : row) fromSpans(cell);
    }
    return out;
}

Target splitTarget(std::string_view target) {
    Target t;
    if (startsWith(target, "http://") || startsWith(target, "https://") || startsWith(target, "mailto:")) {
        t.external = true;
        t.file = std::string(target);
        return t;
    }
    const size_t hash = target.find('#');
    t.file = std::string(target.substr(0, hash));
    if (hash != std::string_view::npos) t.anchor = std::string(target.substr(hash + 1));
    return t;
}

// ---------------------------------------------------------------------------
// Book
// ---------------------------------------------------------------------------

bool Book::load(Loader loader, void* user, std::string& error) {
    chapters_.clear();
    std::string text;
    if (!loader("index.md", text, user)) {
        error = "index.md not found";
        return false;
    }
    index_ = parse(text);
    std::string group;
    for (const auto& b : index_.blocks) {
        if (b.kind == BlockKind::Heading && b.level == 2) group = plainText(b.spans);
        if (b.kind != BlockKind::Numbered && b.kind != BlockKind::Bullet) continue;
        for (const auto& s : b.spans) {
            const Target t = splitTarget(s.link);
            if (s.link.empty() || t.external || t.file.size() < 4 || t.file.substr(t.file.size() - 3) != ".md") continue;
            if (find(t.file) >= 0) continue;
            Chapter c;
            c.file = t.file;
            c.group = group;
            std::string chapterText;
            if (!loader(c.file, chapterText, user)) {
                error = "Chapter " + c.file + " (listed in index.md) not found";
                return false;
            }
            c.doc = parse(chapterText);
            c.title = c.doc.title.empty() ? plainText(b.spans) : c.doc.title;
            chapters_.push_back(std::move(c));
        }
    }
    if (chapters_.empty()) {
        error = "index.md lists no chapters";
        return false;
    }
    return true;
}

int Book::find(std::string_view file) const {
    for (size_t i = 0; i < chapters_.size(); ++i)
        if (chapters_[i].file == file) return int(i);
    return -1;
}

std::vector<SearchHit> Book::search(std::string_view query, size_t limit) const {
    std::vector<std::string> words;
    {
        const std::string q = lower(query);
        size_t k = 0;
        while (k < q.size()) {
            while (k < q.size() && isSpace(q[k])) ++k;
            size_t e = k;
            while (e < q.size() && !isSpace(q[e])) ++e;
            if (e > k) words.push_back(q.substr(k, e - k));
            k = e;
        }
    }
    std::vector<SearchHit> hits;
    if (words.empty()) return hits;
    for (size_t c = 0; c < chapters_.size(); ++c) {
        const Chapter& ch = chapters_[c];
        std::string section, anchor;
        for (size_t bi = 0; bi < ch.doc.blocks.size(); ++bi) {
            const Block& b = ch.doc.blocks[bi];
            const std::string text = plainText(b);
            if (b.kind == BlockKind::Heading && b.level >= 2) {
                section = text;
                anchor = b.anchor;
            }
            const std::string low = lower(text);
            size_t first = std::string::npos;
            bool all = true;
            for (const auto& w : words) {
                const size_t at = low.find(w);
                if (at == std::string::npos) {
                    all = false;
                    break;
                }
                first = std::min(first, at);
            }
            if (!all) continue;
            SearchHit h;
            h.chapter = int(c);
            h.block = int(bi);
            h.section = b.kind == BlockKind::Heading && b.level == 1 ? std::string() : section;
            h.anchor = b.kind == BlockKind::Heading && b.level == 1 ? std::string() : anchor;
            h.score = b.kind == BlockKind::Heading ? (b.level == 1 ? 30 : 20) : 1;
            for (const auto& w : words) // whole-word matches rank above parts of words
                for (size_t at = low.find(w); at != std::string::npos; at = low.find(w, at + 1)) {
                    const bool startOk = at == 0 || !std::isalnum(static_cast<unsigned char>(low[at - 1]));
                    const bool endOk = at + w.size() >= low.size() || !std::isalnum(static_cast<unsigned char>(low[at + w.size()]));
                    h.score += startOk && endOk ? 3 : 1;
                }
            // A window of text around the first match, cut at word boundaries.
            const size_t radius = 70;
            size_t from = first > radius ? first - radius : 0;
            size_t to = std::min(text.size(), first + words.front().size() + radius);
            while (from > 0 && !isSpace(text[from - 1])) --from;
            while (to < text.size() && !isSpace(text[to])) ++to;
            while (from > 0 && (static_cast<unsigned char>(text[from]) & 0xC0) == 0x80) --from;
            std::string snip = text.substr(from, to - from);
            for (auto& ch2 : snip)
                if (ch2 == '\n' || ch2 == '\t') ch2 = ' ';
            h.matchStart = first - from;
            h.matchLength = words.front().size();
            for (const auto& w : words) // the first word that occurs earliest is the one to highlight
                if (low.find(w) == first) {
                    h.matchLength = w.size();
                    break;
                }
            if (from > 0) {
                snip = "..." + snip;
                h.matchStart += 3;
            }
            if (to < text.size()) snip += "...";
            h.snippet = std::move(snip);
            // One result per section: later matches add to its score, and text beats the heading as the snippet.
            if (!hits.empty() && hits.back().chapter == h.chapter && hits.back().anchor == h.anchor) {
                SearchHit& prev = hits.back();
                const bool prevIsHeading = ch.doc.blocks[size_t(prev.block)].kind == BlockKind::Heading;
                const int total = prev.score + h.score;
                if (prevIsHeading && b.kind != BlockKind::Heading) prev = std::move(h);
                prev.score = total;
                continue;
            }
            hits.push_back(std::move(h));
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) { return a.score > b.score; });
    if (hits.size() > limit) hits.resize(limit);
    return hits;
}

} // namespace sf::guide
