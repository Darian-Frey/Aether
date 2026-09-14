#include "rule/dsl.hpp"

#include "rule/decay.hpp"
#include "rule/table_layout.hpp"

#include <cctype>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace aether::rule {

namespace {

// --- Shared helpers ------------------------------------------------------------

DslResult fail(uint32_t line, uint32_t column, std::string message) {
    DslResult r;
    r.error = ParseError{line, column, std::move(message)};
    return r;
}

// Runs IR validation as the last step of every notation. A front end that
// emits an invalid IR is a front-end bug, but reporting it here means the bug
// surfaces as a compile error rather than as undefined behaviour downstream.
DslResult finish(RuleIR ir) {
    const auto diagnostics = validate(ir);
    if (!diagnostics.empty()) {
        return fail(1, 1, "internal: front end produced an invalid IR: " + diagnostics.front().message);
    }
    DslResult r;
    r.ir = std::move(ir);
    return r;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

// --- Life-like and Generations ------------------------------------------------
//
//   rule := "B" digits "/" "S" digits [ "/" "C" integer ]

struct LifeLike {
    std::vector<bool> birth;      // indexed by neighbour count
    std::vector<bool> survive;
    uint16_t          states = 2;
};

// Returns nullopt if `s` is not in B/S form at all (so the table block can be
// tried), or an error if it is B/S form but malformed.
std::optional<std::variant<LifeLike, ParseError>> tryLifeLike(std::string_view s, uint32_t N) {
    s = trim(s);
    if (s.empty() || (s[0] != 'B' && s[0] != 'b')) return std::nullopt;

    auto column = [&](size_t i) { return static_cast<uint32_t>(i + 1); };
    auto err = [&](size_t i, std::string m) -> std::variant<LifeLike, ParseError> {
        return ParseError{1, column(i), std::move(m)};
    };

    LifeLike out;
    out.birth.assign(N + 1, false);
    out.survive.assign(N + 1, false);

    size_t i = 1;
    auto readDigits = [&](std::vector<bool>& into) -> std::optional<ParseError> {
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            const uint32_t k = static_cast<uint32_t>(s[i] - '0');
            if (k > N) {
                return ParseError{1, column(i),
                    std::format("count {} exceeds the neighbourhood size {}", k, N)};
            }
            into[k] = true;
            ++i;
        }
        return std::nullopt;
    };

    if (auto e = readDigits(out.birth)) return *e;
    if (i >= s.size() || s[i] != '/') return err(i, "expected '/' after birth counts");
    ++i;
    if (i >= s.size() || (s[i] != 'S' && s[i] != 's')) return err(i, "expected 'S' after '/'");
    ++i;
    if (auto e = readDigits(out.survive)) return *e;

    if (i < s.size() && s[i] == '/') {
        ++i;
        if (i >= s.size() || (s[i] != 'C' && s[i] != 'c')) return err(i, "expected 'C' after second '/'");
        ++i;
        const size_t start = i;
        uint32_t c = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            c = c * 10 + static_cast<uint32_t>(s[i] - '0');
            if (c > 256) break;
            ++i;
        }
        if (i == start) return err(i, "expected a state count after 'C'");
        if (c < 2 || c > 256) return err(start, std::format("state count must be in 2..256"));
        out.states = static_cast<uint16_t>(c);
    }

    if (i != s.size()) return err(i, std::format("unexpected '{}'", s[i]));
    return out;
}

// Generations semantics on the outer-totalistic count vector: only the count
// of state 1 matters. State 0 is born into 1; state 1 survives or decays into
// 2; states 2..C-1 advance unconditionally and the last decays to 0. With
// C == 2 this is plain Life-like.
Table buildLifeLikeTable(const LifeLike& rule, const TableLayout& layout) {
    Table t;
    t.entries.assign(*layout.size(), 0);
    const uint16_t C = rule.states;
    for (uint8_t own = 0; own < C; ++own) {
        layout.forEachCountVector([&](std::span<const uint32_t> counts) {
            const uint32_t alive = counts[0];
            uint8_t next;
            if (own == 0) {
                next = rule.birth[alive] ? 1 : 0;
            } else if (own == 1) {
                next = rule.survive[alive] ? 1 : static_cast<uint8_t>(C == 2 ? 0 : 2);
            } else {
                next = static_cast<uint8_t>(own + 1 < C ? own + 1 : 0);
            }
            t.entries[layout.indexOuterTotalistic(own, counts)] = next;
        });
    }
    return t;
}

// --- Table block ----------------------------------------------------------------
//
//   rule_block := header statement*
//   header     := "states" integer ";"
//                 "neighbourhood" ("moore"|"von_neumann"|"hex"|"hexagonal") integer ";"
//                 [ "boundary" ("wrap"|"zero"|"mirror") ";" ]
//                 [ "decay" integer ";" ]
//   statement  := integer ":" condition "->" integer ";"
//   condition  := count_expr
//   count_expr := "n" "(" integer ")" comparison integer
//                 { ("and"|"or") count_expr }
//
// `and` binds tighter than `or`. Signature literals (SPEC §7 mentions them
// without defining them) are not accepted yet.

enum class Tok { Ident, Int, Semi, Colon, Arrow, LParen, RParen, LBracket, RBracket, Comma, Underscore, Cmp, End };

struct Token {
    Tok         kind;
    std::string text;
    uint32_t    line;
    uint32_t    column;
};

class Lexer {
public:
    explicit Lexer(std::string_view src) : src_(src) {}

    std::variant<std::vector<Token>, ParseError> run() {
        std::vector<Token> out;
        while (true) {
            skipSpace();
            if (pos_ >= src_.size()) {
                out.push_back({Tok::End, "", line_, col_});
                return out;
            }
            const char c = src_[pos_];
            const uint32_t line = line_, col = col_;
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                const size_t start = pos_;
                while (pos_ < src_.size() &&
                       (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_')) advance();
                out.push_back({Tok::Ident, std::string(src_.substr(start, pos_ - start)), line, col});
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                const size_t start = pos_;
                while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) advance();
                out.push_back({Tok::Int, std::string(src_.substr(start, pos_ - start)), line, col});
            } else if (c == ';') { advance(); out.push_back({Tok::Semi, ";", line, col}); }
            else if (c == ':')   { advance(); out.push_back({Tok::Colon, ":", line, col}); }
            else if (c == '(')   { advance(); out.push_back({Tok::LParen, "(", line, col}); }
            else if (c == ')')   { advance(); out.push_back({Tok::RParen, ")", line, col}); }
            else if (c == '[')   { advance(); out.push_back({Tok::LBracket, "[", line, col}); }
            else if (c == ']')   { advance(); out.push_back({Tok::RBracket, "]", line, col}); }
            else if (c == ',')   { advance(); out.push_back({Tok::Comma, ",", line, col}); }
            else if (c == '-' && peek(1) == '>') { advance(); advance(); out.push_back({Tok::Arrow, "->", line, col}); }
            else if ((c == '=' || c == '!') && peek(1) == '=') {
                advance(); advance(); out.push_back({Tok::Cmp, std::string(1, c) + "=", line, col});
            } else if (c == '<' || c == '>') {
                advance();
                std::string t(1, c);
                if (peek(0) == '=') { advance(); t += '='; }
                out.push_back({Tok::Cmp, t, line, col});
            } else {
                return ParseError{line, col, std::format("unexpected character '{}'", c)};
            }
        }
    }

private:
    char peek(size_t ahead) const { return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0'; }
    void advance() {
        if (src_[pos_] == '\n') { ++line_; col_ = 1; } else { ++col_; }
        ++pos_;
    }
    void skipSpace() {
        while (pos_ < src_.size()) {
            const char c = src_[pos_];
            if (std::isspace(static_cast<unsigned char>(c))) { advance(); continue; }
            if (c == '#') {   // comment to end of line
                while (pos_ < src_.size() && src_[pos_] != '\n') advance();
                continue;
            }
            break;
        }
    }

    std::string_view src_;
    size_t   pos_  = 0;
    uint32_t line_ = 1;
    uint32_t col_  = 1;
};

// A parsed count condition, kept as a tree so it can be both evaluated (to
// fill a table) and lowered to an Expression (when the table is too large).
struct Cond {
    enum class Op { Cmp, Signature, And, Or } op = Op::Cmp;
    uint32_t    state = 0;      // Cmp: n(state)
    std::string cmp;            // Cmp: operator
    uint32_t    value = 0;      // Cmp: right-hand side
    // Signature: the pattern and, when `rot` was given, its rotations.
    // -1 is a wildcard. A cell matches if any pattern does.
    std::vector<std::vector<int>> patterns;
    std::vector<Cond> kids;     // And/Or: exactly two
};

struct Statement {
    uint32_t own;
    Cond     cond;
    uint32_t next;
    uint32_t line, column;
};

struct Block {
    uint16_t      states = 0;
    Neighbourhood nb;
    std::optional<Boundary> boundary;
    uint16_t      decay = 0;
    bool          hasSignature = false;   // any literal makes the rule non-totalistic
    std::vector<Statement> statements;
};

class BlockParser {
public:
    BlockParser(std::vector<Token> toks, uint8_t dimensions)
        : toks_(std::move(toks)), dimensions_(dimensions) {}

    std::variant<Block, ParseError> run() {
        Block b;
        if (auto e = expectIdent("states")) return *e;
        if (auto v = integer(2, 256, "state count"); v) b.states = static_cast<uint16_t>(*v); else return err_;
        if (auto e = expect(Tok::Semi, "';'")) return *e;

        if (auto e = expectIdent("neighbourhood")) return *e;
        {
            const Token& t = cur();
            if (t.kind != Tok::Ident) return err(t, "expected 'moore', 'von_neumann' or 'hex'");
            const auto type = parseNeighbourhoodType(t.text);
            if (!type) return err(t, std::format("unknown neighbourhood '{}'", t.text));
            b.nb.type = *type;
            ++i_;
        }
        if (auto v = integer(1, 127, "radius"); v) b.nb.radius = static_cast<uint8_t>(*v); else return err_;
        if (auto e = expect(Tok::Semi, "';'")) return *e;

        if (cur().kind == Tok::Ident && cur().text == "boundary") {
            ++i_;
            const Token& t = cur();
            if (t.kind != Tok::Ident) return err(t, "expected 'wrap', 'zero' or 'mirror'");
            const auto bd = parseBoundary(t.text);
            if (!bd) return err(t, std::format("unknown boundary '{}'", t.text));
            b.boundary = *bd;
            ++i_;
            if (auto e = expect(Tok::Semi, "';'")) return *e;
        }

        if (cur().kind == Tok::Ident && cur().text == "decay") {
            decayToken_ = i_;
            ++i_;
            if (auto v = integer(0, 254, "decay length"); v) b.decay = static_cast<uint16_t>(*v); else return err_;
            if (auto e = expect(Tok::Semi, "';'")) return *e;
        }

        while (cur().kind != Tok::End) {
            Statement s;
            s.line = cur().line;
            s.column = cur().column;
            if (auto v = integer(0, b.states - 1u, "state"); v) s.own = *v; else return err_;
            if (auto e = expect(Tok::Colon, "':'")) return *e;
            if (auto c = orExpr(b.states, b); c) s.cond = std::move(*c); else return err_;
            if (auto e = expect(Tok::Arrow, "'->'")) return *e;
            if (auto v = integer(0, b.states - 1u, "state"); v) s.next = *v; else return err_;
            if (auto e = expect(Tok::Semi, "';'")) return *e;
            b.statements.push_back(std::move(s));
        }
        return b;
    }

private:
    const Token& cur() const { return toks_[i_]; }

    ParseError err(const Token& t, std::string m) {
        err_ = ParseError{t.line, t.column, std::move(m)};
        return err_;
    }

    std::optional<ParseError> expect(Tok k, const char* what) {
        if (cur().kind != k) return err(cur(), std::format("expected {}", what));
        ++i_;
        return std::nullopt;
    }

    std::optional<ParseError> expectIdent(const char* word) {
        if (cur().kind != Tok::Ident || cur().text != word) {
            return err(cur(), std::format("expected '{}'", word));
        }
        ++i_;
        return std::nullopt;
    }

    std::optional<uint32_t> integer(uint32_t lo, uint32_t hi, const char* what) {
        const Token& t = cur();
        if (t.kind != Tok::Int) { err(t, std::format("expected {}", what)); return std::nullopt; }
        uint64_t v = 0;
        for (char c : t.text) {
            v = v * 10 + static_cast<uint64_t>(c - '0');
            if (v > hi) break;
        }
        if (v < lo || v > hi) {
            err(t, std::format("{} must be in {}..{} (got {})", what, lo, hi, t.text));
            return std::nullopt;
        }
        ++i_;
        return static_cast<uint32_t>(v);
    }

    std::optional<Cond> orExpr(uint16_t states, Block& b) {
        auto lhs = andExpr(states, b);
        if (!lhs) return std::nullopt;
        while (cur().kind == Tok::Ident && cur().text == "or") {
            ++i_;
            auto rhs = andExpr(states, b);
            if (!rhs) return std::nullopt;
            Cond c;
            c.op = Cond::Op::Or;
            c.kids = {std::move(*lhs), std::move(*rhs)};
            lhs = std::move(c);
        }
        return lhs;
    }

    std::optional<Cond> andExpr(uint16_t states, Block& b) {
        auto lhs = term(states, b);
        if (!lhs) return std::nullopt;
        while (cur().kind == Tok::Ident && cur().text == "and") {
            ++i_;
            auto rhs = term(states, b);
            if (!rhs) return std::nullopt;
            Cond c;
            c.op = Cond::Op::And;
            c.kids = {std::move(*lhs), std::move(*rhs)};
            lhs = std::move(c);
        }
        return lhs;
    }

    // A literal, or a count condition.
    std::optional<Cond> term(uint16_t states, Block& b) {
        if (cur().kind == Tok::LBracket) return signatureLiteral(states, b);
        return countExpr(states);
    }

    std::optional<Cond> signatureLiteral(uint16_t states, Block& b) {
        const Token& open = cur();
        ++i_;
        const uint32_t n = neighbourCount(dimensions_, b.nb);
        std::vector<int> pattern;
        for (;;) {
            if (cur().kind == Tok::Ident && cur().text == "_") { pattern.push_back(-1); ++i_; }
            else if (auto v = integer(0, states - 1u, "state"); v) pattern.push_back(static_cast<int>(*v));
            else return std::nullopt;
            if (cur().kind == Tok::Comma) { ++i_; continue; }
            break;
        }
        if (expect(Tok::RBracket, "']'")) return std::nullopt;
        if (pattern.size() != n) {
            err(open, std::format("signature has {} elements; this neighbourhood has {} neighbours",
                                  pattern.size(), n));
            return std::nullopt;
        }

        Cond c;
        c.op = Cond::Op::Signature;
        c.patterns.push_back(pattern);
        if (cur().kind == Tok::Ident && cur().text == "rot") {
            const Token& rotTok = cur();
            ++i_;
            const auto perm = rotationPermutation(dimensions_, b.nb);
            if (!perm) {
                err(rotTok, "rot is defined for 2D lattices only; a rotation elsewhere would have to pick an axis");
                return std::nullopt;
            }
            std::vector<int> v = pattern;
            for (int turn = 1; turn < 8; ++turn) {
                std::vector<int> next(n);
                for (uint32_t i = 0; i < n; ++i) next[(*perm)[i]] = v[i];
                if (next == pattern) break;
                c.patterns.push_back(next);
                v = std::move(next);
            }
        }
        b.hasSignature = true;
        return c;
    }

    std::optional<Cond> countExpr(uint16_t states) {
        Cond c;
        if (cur().kind != Tok::Ident || cur().text != "n") { err(cur(), "expected 'n(' or a signature literal"); return std::nullopt; }
        ++i_;
        if (expect(Tok::LParen, "'('")) return std::nullopt;
        if (auto v = integer(0, states - 1u, "state"); v) c.state = *v; else return std::nullopt;
        if (expect(Tok::RParen, "')'")) return std::nullopt;
        if (cur().kind != Tok::Cmp) { err(cur(), "expected a comparison operator"); return std::nullopt; }
        c.cmp = cur().text;
        ++i_;
        if (auto v = integer(0, 1u << 20, "count"); v) c.value = *v; else return std::nullopt;
        return c;
    }

public:
    // Where `decay` appeared, so the compiler can point at it if the tail
    // does not fit.
    size_t decayToken() const { return decayToken_; }
    const Token& token(size_t i) const { return toks_[i]; }

private:
    std::vector<Token> toks_;
    uint8_t    dimensions_ = 2;
    size_t     i_ = 0;
    size_t     decayToken_ = 0;
    ParseError err_;
};

bool compare(std::string_view cmp, uint32_t a, uint32_t b) {
    if (cmp == "==") return a == b;
    if (cmp == "!=") return a != b;
    if (cmp == "<")  return a <  b;
    if (cmp == "<=") return a <= b;
    if (cmp == ">")  return a >  b;
    return a >= b;
}

// counts is indexed by state 1..S-1 at positions 0..S-2; state 0's count is
// derived from N. `nbr` is the neighbour vector in canonical order, and is
// empty on the outer-totalistic path, where no literal can appear.
bool evalCond(const Cond& c, std::span<const uint32_t> counts, std::span<const uint8_t> nbr, uint32_t N) {
    switch (c.op) {
        case Cond::Op::And: return evalCond(c.kids[0], counts, nbr, N) && evalCond(c.kids[1], counts, nbr, N);
        case Cond::Op::Or:  return evalCond(c.kids[0], counts, nbr, N) || evalCond(c.kids[1], counts, nbr, N);
        case Cond::Op::Signature: {
            for (const std::vector<int>& pat : c.patterns) {
                bool ok = true;
                for (size_t i = 0; i < pat.size() && ok; ++i) {
                    if (pat[i] >= 0 && static_cast<uint8_t>(pat[i]) != nbr[i]) ok = false;
                }
                if (ok) return true;
            }
            return false;
        }
        case Cond::Op::Cmp: {
            uint32_t n;
            if (c.state == 0) {
                uint32_t sum = 0;
                for (uint32_t v : counts) sum += v;
                n = N - sum;
            } else {
                n = counts[c.state - 1];
            }
            return compare(c.cmp, n, c.value);
        }
    }
    return false;
}

Table buildBlockTable(const Block& b, const TableLayout& layout, uint32_t N) {
    Table t;
    t.entries.assign(*layout.size(), 0);
    for (uint32_t own = 0; own < b.states; ++own) {
        layout.forEachCountVector([&](std::span<const uint32_t> counts) {
            uint32_t next = own;   // no match: retain
            for (const Statement& s : b.statements) {
                if (s.own == own && evalCond(s.cond, counts, {}, N)) { next = s.next; break; }
            }
            t.entries[layout.indexOuterTotalistic(static_cast<uint8_t>(own), counts)] =
                static_cast<uint8_t>(next);
        });
    }
    return t;
}

// Every (own, neighbour vector) pair, decoded from the signature the way
// TableLayout indexes it: base S, little-endian, canonical neighbour order.
Table buildSignatureTable(const Block& b, const TableLayout& layout, uint32_t N) {
    Table t;
    t.entries.assign(*layout.size(), 0);
    const uint32_t S = b.states;
    uint64_t signatures = 1;
    for (uint32_t i = 0; i < N; ++i) signatures *= S;

    std::vector<uint8_t>  nbr(N);
    std::vector<uint32_t> counts(S > 1 ? S - 1u : 0u);
    for (uint64_t sig = 0; sig < signatures; ++sig) {
        uint64_t rest = sig;
        for (uint32_t& c : counts) c = 0;
        for (uint32_t i = 0; i < N; ++i) {
            nbr[i] = static_cast<uint8_t>(rest % S);
            rest /= S;
            if (nbr[i] != 0) ++counts[nbr[i] - 1u];
        }
        for (uint32_t own = 0; own < S; ++own) {
            uint32_t next = own;   // no match: retain
            for (const Statement& s : b.statements) {
                if (s.own == own && evalCond(s.cond, counts, nbr, N)) { next = s.next; break; }
            }
            t.entries[layout.indexNonTotalistic(static_cast<uint8_t>(own), nbr)] = static_cast<uint8_t>(next);
        }
    }
    return t;
}

// Lowers the statement list to a Select chain. Statements are tried in
// order, so the chain nests from the last statement outwards.
class ExprBuilder {
public:
    uint32_t add(ExprNode n) { nodes_.push_back(n); return static_cast<uint32_t>(nodes_.size() - 1); }
    uint32_t literal(int64_t v) { return add({ExprOp::IntLiteral, 0, 0, 0, v}); }

    uint32_t cond(const Cond& c) {
        switch (c.op) {
            case Cond::Op::And: { const auto a = cond(c.kids[0]); const auto b = cond(c.kids[1]); return add({ExprOp::And, a, b}); }
            case Cond::Op::Or:  { const auto a = cond(c.kids[0]); const auto b = cond(c.kids[1]); return add({ExprOp::Or,  a, b}); }
            case Cond::Op::Signature:
                // Unreachable: a rule with a literal is refused above rather
                // than lowered, until the codegen backend exists.
                return literal(0);
            case Cond::Op::Cmp: {
                const auto n = add({ExprOp::Count, c.state});
                const auto v = literal(c.value);
                ExprOp op = ExprOp::Eq;
                if (c.cmp == "!=") op = ExprOp::Ne;
                else if (c.cmp == "<")  op = ExprOp::Lt;
                else if (c.cmp == "<=") op = ExprOp::Le;
                else if (c.cmp == ">")  op = ExprOp::Gt;
                else if (c.cmp == ">=") op = ExprOp::Ge;
                return add({op, n, v});
            }
        }
        return 0;
    }

    Expression build(const Block& b) {
        // Innermost default: retain own state.
        uint32_t chain = add({ExprOp::Self});
        for (size_t i = b.statements.size(); i-- > 0;) {
            const Statement& s = b.statements[i];
            const auto self = add({ExprOp::Self});
            const auto own  = literal(s.own);
            const auto isOwn = add({ExprOp::Eq, self, own});
            const auto c    = cond(s.cond);
            const auto both = add({ExprOp::And, isOwn, c});
            const auto next = literal(s.next);
            chain = add({ExprOp::Select, both, next, chain});
        }
        Expression e;
        e.nodes = std::move(nodes_);
        return e;
    }

private:
    std::vector<ExprNode> nodes_;
};

}  // namespace

// --- Entry point ----------------------------------------------------------------

DslResult parseDsl(std::string_view source, const DslContext& ctx) {
    if (ctx.dimensions < 1 || ctx.dimensions > 3) {
        return fail(1, 1, std::format("session dimensions must be 1, 2 or 3 (got {})", ctx.dimensions));
    }
    if (trim(source).empty()) return fail(1, 1, "empty rule");

    // 1 & 2: Life-like and Generations share a shape.
    const Neighbourhood moore1{NeighbourhoodType::Moore, 1};
    const uint32_t mooreN = neighbourCount(ctx.dimensions, moore1);
    if (auto ll = tryLifeLike(source, mooreN)) {
        if (const auto* e = std::get_if<ParseError>(&*ll)) {
            DslResult r; r.error = *e; return r;
        }
        const LifeLike& rule = std::get<LifeLike>(*ll);
        RuleIR ir;
        ir.dimensions    = ctx.dimensions;
        ir.states        = rule.states;
        ir.neighbourhood = moore1;
        ir.boundary      = ctx.boundary;
        ir.kind          = Kind::OuterTotalistic;
        ir.metadata.source_notation = std::string(trim(source));

        const TableLayout layout(ir.kind, ir.states, mooreN);
        if (!layout.size() || *layout.size() > kLutMaxEntries) {
            // Generations with many states (IMP-001). Lower to an expression
            // via a synthetic block so the two paths share one lowering.
            Block b;
            b.states = rule.states;
            b.nb = moore1;
            for (uint32_t k = 0; k <= mooreN; ++k) {
                if (rule.birth[k])   b.statements.push_back({0, Cond{Cond::Op::Cmp, 1, "==", k, {}, {}}, 1, 1, 1});
            }
            for (uint32_t k = 0; k <= mooreN; ++k) {
                if (rule.survive[k]) b.statements.push_back({1, Cond{Cond::Op::Cmp, 1, "==", k, {}, {}}, 1, 1, 1});
            }
            b.statements.push_back({1, Cond{Cond::Op::Cmp, 1, ">=", 0, {}, {}}, rule.states == 2 ? 0u : 2u, 1, 1});
            for (uint32_t s = 2; s < rule.states; ++s) {
                b.statements.push_back({s, Cond{Cond::Op::Cmp, 1, ">=", 0, {}, {}}, s + 1 < rule.states ? s + 1 : 0, 1, 1});
            }
            ir.kind = Kind::Expression;
            ir.transition = ExprBuilder().build(b);
        } else {
            ir.transition = buildLifeLikeTable(rule, layout);
        }
        return finish(std::move(ir));
    }

    // 3: table block.
    auto lexed = Lexer(source).run();
    if (const auto* e = std::get_if<ParseError>(&lexed)) { DslResult r; r.error = *e; return r; }
    BlockParser parser(std::move(std::get<std::vector<Token>>(lexed)), ctx.dimensions);
    auto parsed = parser.run();
    if (const auto* e = std::get_if<ParseError>(&parsed)) { DslResult r; r.error = *e; return r; }
    const Block& b = std::get<Block>(parsed);

    RuleIR ir;
    ir.dimensions    = ctx.dimensions;
    ir.states        = b.states;
    ir.neighbourhood = b.nb;
    ir.boundary      = b.boundary.value_or(ctx.boundary);
    ir.kind          = b.hasSignature ? Kind::NonTotalistic : Kind::OuterTotalistic;
    ir.metadata.source_notation = std::string(trim(source));

    const uint32_t N = neighbourCount(ir.dimensions, ir.neighbourhood);
    if (b.hasSignature && N > 64) {
        return fail(1, 1, std::format("a signature rule needs at most 64 neighbours; this one has {}", N));
    }
    const TableLayout layout(ir.kind, ir.states, N);
    if (layout.size() && *layout.size() <= kLutMaxEntries) {
        ir.transition = b.hasSignature ? buildSignatureTable(b, layout, N) : buildBlockTable(b, layout, N);
    } else if (b.hasSignature) {
        return fail(1, 1, std::format("a signature rule over {} states and {} neighbours needs {} table entries, "
                                      "against a limit of {}; expression lowering for these arrives with the "
                                      "codegen backend",
                                      ir.states, N,
                                      layout.size() ? std::to_string(*layout.size()) : "more than 2^64",
                                      kLutMaxEntries));
    } else if (b.decay > 0) {
        return fail(1, 1, "this rule is already too large for a table, so it cannot take a decay tail");
    } else {
        ir.kind = Kind::Expression;
        ir.transition = ExprBuilder().build(b);
    }

    if (b.decay > 0) {
        auto decayed = applyDecay(ir, b.decay);
        if (const auto* e = std::get_if<std::string>(&decayed)) {
            const Token& t = parser.token(parser.decayToken());
            return fail(t.line, t.column, *e);
        }
        ir = std::get<RuleIR>(std::move(decayed));
        ir.metadata.source_notation = std::string(trim(source));
    }
    return finish(std::move(ir));
}

}  // namespace aether::rule
