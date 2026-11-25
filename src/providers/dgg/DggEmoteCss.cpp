#include "providers/dgg/DggEmoteCss.hpp"

#include "providers/dgg/DggCombo.hpp"
#include "providers/dgg/DggCssAnimation.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <optional>
#include <regex>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace chatterino {

using namespace dggcss;

namespace {

using std::string;
using std::string_view;

constexpr size_t NPOS = string::npos;

/// An animation with more steps than this isn't a sprite animation we want
/// to cut up (and it would need an absurd amount of memory).
constexpr int MAX_STEPS = 1000;

using Props = std::unordered_map<string, string>;
using Keyframes = std::unordered_map<string, std::vector<dggcss::KeyframeStop>>;

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool isIdentChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ||
           c == '-' || static_cast<unsigned char>(c) >= 0x80;
}

string_view trimView(string_view s)
{
    while (!s.empty() && isSpace(s.front()))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && isSpace(s.back()))
    {
        s.remove_suffix(1);
    }
    return s;
}

string toLower(string_view s)
{
    string out(s);
    for (auto &c : out)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

bool startsWith(string_view s, string_view prefix)
{
    return s.substr(0, prefix.size()) == prefix;
}

bool contains(const std::vector<string> &list, string_view needle)
{
    return std::find(list.begin(), list.end(), needle) != list.end();
}

/// Splits at `sep`, but not inside quotes, parentheses or brackets.
std::vector<string> splitTopLevel(string_view s, char sep)
{
    std::vector<string> out;
    string cur;
    int depth = 0;
    char quote = 0;
    bool escaped = false;

    for (const char c : s)
    {
        if (quote != 0)
        {
            cur.push_back(c);
            if (escaped)
            {
                escaped = false;
            }
            else if (c == '\\')
            {
                escaped = true;
            }
            else if (c == quote || c == '\n')  // a newline ends a string
            {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
            cur.push_back(c);
            continue;
        }
        if (c == '(' || c == '[')
        {
            depth++;
        }
        else if (c == ')' || c == ']')
        {
            depth--;
        }

        if (c == sep && depth <= 0)
        {
            out.push_back(std::move(cur));
            cur.clear();
        }
        else
        {
            cur.push_back(c);
        }
    }
    out.push_back(std::move(cur));
    return out;
}

/// Splits at whitespace, but not inside quotes or parentheses.
std::vector<string> splitWhitespace(string_view s)
{
    std::vector<string> out;
    string cur;
    int depth = 0;
    char quote = 0;
    bool escaped = false;

    for (const char c : s)
    {
        if (quote != 0)
        {
            cur.push_back(c);
            if (escaped)
            {
                escaped = false;
            }
            else if (c == '\\')
            {
                escaped = true;
            }
            else if (c == quote || c == '\n')  // a newline ends a string
            {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
            cur.push_back(c);
            continue;
        }
        if (c == '(')
        {
            depth++;
        }
        else if (c == ')')
        {
            depth--;
        }

        if (isSpace(c) && depth <= 0)
        {
            if (!cur.empty())
            {
                out.push_back(std::move(cur));
                cur.clear();
            }
        }
        else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
    {
        out.push_back(std::move(cur));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

/// Parses a number starting at `pos` and moves `pos` behind it.
std::optional<double> parseNumberAt(string_view s, size_t &pos)
{
    // Not strtod(): that follows the C locale, which Qt sets from the
    // environment on Unix. See parseNumberPrefix().
    if (pos > s.size())
    {
        return std::nullopt;
    }
    size_t consumed = 0;
    const auto value = anim::parseNumberPrefix(s.substr(pos), consumed);
    if (!value)
    {
        return std::nullopt;
    }
    pos += consumed;
    return value;
}

/// A bare number ("1.25", "3").
std::optional<double> parseNumber(string_view s)
{
    s = trimView(s);
    size_t pos = 0;
    auto value = parseNumberAt(s, pos);
    if (!value || pos != s.size())
    {
        return std::nullopt;
    }
    return value;
}

/// `12px`, `-0.5px`, or a bare `0`.
std::optional<double> parseLengthPx(string_view s)
{
    s = trimView(s);
    size_t pos = 0;
    auto value = parseNumberAt(s, pos);
    if (!value)
    {
        return std::nullopt;
    }
    const auto unit = s.substr(pos);
    if (unit == "px" || (unit.empty() && *value == 0.0))
    {
        return value;
    }
    return std::nullopt;
}

/// `1.3s`, `.67s`, `250ms`, in milliseconds.
std::optional<double> parseTimeMs(string_view s)
{
    s = trimView(s);
    size_t pos = 0;
    auto value = parseNumberAt(s, pos);
    if (!value)
    {
        return std::nullopt;
    }
    const auto unit = s.substr(pos);
    if (unit == "s")
    {
        return *value * 1000.0;
    }
    if (unit == "ms")
    {
        return *value;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Block scanning
// ---------------------------------------------------------------------------

string stripComments(string_view css)
{
    string out;
    out.reserve(css.size());

    size_t i = 0;
    while (i < css.size())
    {
        if (css[i] == '/' && i + 1 < css.size() && css[i + 1] == '*')
        {
            const auto end = css.find("*/", i + 2);
            if (end == NPOS)
            {
                break;  // unterminated comment swallows the rest
            }
            i = end + 2;
            out.push_back(' ');
            continue;
        }
        if (css[i] != '\r')
        {
            out.push_back(css[i]);
        }
        i++;
    }
    return out;
}

/// The index of the `}` that closes the `{` at `open`, or NPOS.
size_t findClosingBrace(string_view s, size_t open)
{
    int depth = 0;
    char quote = 0;
    for (size_t i = open; i < s.size(); i++)
    {
        const char c = s[i];
        if (quote != 0)
        {
            if (c == '\\')
            {
                i++;
            }
            else if (c == quote || c == '\n')
            {
                // A newline ends a string (CSS: "bad string"). Without it a
                // stray quote would swallow the rest of the stylesheet.
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
        }
        else if (c == '{')
        {
            depth++;
        }
        else if (c == '}')
        {
            depth--;
            if (depth == 0)
            {
                return i;
            }
        }
    }
    return NPOS;
}

/// Calls `fn(prelude, body)` for each `prelude { body }` in `css`. Statements
/// like `@import ...;` and stray text are skipped.
template <typename Fn>
void forEachBlock(string_view css, Fn &&fn)
{
    size_t i = 0;
    while (i < css.size())
    {
        const auto brace = css.find('{', i);
        if (brace == NPOS)
        {
            break;
        }
        const auto semicolon = css.find(';', i);
        if (semicolon != NPOS && semicolon < brace)
        {
            i = semicolon + 1;
            continue;
        }

        const auto close = findClosingBrace(css, brace);
        if (close == NPOS)
        {
            break;
        }
        fn(trimView(css.substr(i, brace - i)),
           css.substr(brace + 1, close - brace - 1));
        i = close + 1;
    }
}

std::vector<Decl> parseDeclarations(string_view body)
{
    std::vector<Decl> out;
    for (const auto &part : splitTopLevel(body, ';'))
    {
        const auto colon = part.find(':');
        if (colon == NPOS)
        {
            continue;
        }

        const auto name = trimView(string_view(part).substr(0, colon));
        auto value = trimView(string_view(part).substr(colon + 1));
        if (name.empty() || startsWith(name, "//"))
        {
            // commented out with `//`, which isn't CSS but is in there
            continue;
        }

        Decl decl;
        decl.name = startsWith(name, "--") ? string(name) : toLower(name);

        const auto bang = value.rfind('!');
        if (bang != NPOS &&
            toLower(trimView(value.substr(bang + 1))) == "important")
        {
            decl.important = true;
            value = trimView(value.substr(0, bang));
        }
        decl.value = string(value);
        out.push_back(std::move(decl));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Selectors
// ---------------------------------------------------------------------------

struct RawPart {
    char combinator;
    string text;
};

std::vector<RawPart> tokenizeSelector(string_view sel)
{
    std::vector<RawPart> out;
    string cur;
    char comb = 0;
    int depth = 0;
    char quote = 0;

    for (const char c : sel)
    {
        if (quote != 0)
        {
            cur.push_back(c);
            if (c == quote)
            {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
            cur.push_back(c);
            continue;
        }
        if (c == '(' || c == '[')
        {
            depth++;
        }
        else if (c == ')' || c == ']')
        {
            depth--;
        }

        const bool combinator = c == '>' || c == '+' || c == '~';
        if (depth == 0 && (isSpace(c) || combinator))
        {
            if (!cur.empty())
            {
                out.push_back(RawPart{comb, std::move(cur)});
                cur.clear();
                comb = 0;
            }
            if (combinator)
            {
                comb = c;
            }
            else if (comb == 0)
            {
                comb = ' ';
            }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty())
    {
        out.push_back(RawPart{comb, std::move(cur)});
    }
    return out;
}

std::optional<Attr> parseAttr(string_view inner)
{
    inner = trimView(inner);
    size_t i = 0;

    Attr attr;
    while (i < inner.size() && isIdentChar(inner[i]))
    {
        attr.name.push_back(inner[i]);
        i++;
    }
    if (attr.name.empty())
    {
        return std::nullopt;
    }
    while (i < inner.size() && isSpace(inner[i]))
    {
        i++;
    }
    if (i == inner.size())
    {
        return attr;
    }

    while (i < inner.size() && string_view("~|^$*=").find(inner[i]) != NPOS)
    {
        attr.op.push_back(inner[i]);
        i++;
    }
    if (attr.op.empty())
    {
        return std::nullopt;
    }
    while (i < inner.size() && isSpace(inner[i]))
    {
        i++;
    }
    if (i == inner.size())
    {
        return std::nullopt;
    }

    if (inner[i] == '"' || inner[i] == '\'')
    {
        const char quote = inner[i];
        const auto end = inner.find(quote, i + 1);
        if (end == NPOS)
        {
            return std::nullopt;
        }
        attr.value = string(inner.substr(i + 1, end - i - 1));
    }
    else
    {
        while (i < inner.size() && !isSpace(inner[i]))
        {
            attr.value.push_back(inner[i]);
            i++;
        }
    }
    return attr;
}

std::optional<Compound> parseCompound(string_view c)
{
    Compound out;
    size_t i = 0;
    const size_t n = c.size();
    if (i < n && c[i] == '*')
    {
        i++;
    }

    while (i < n)
    {
        const char ch = c[i];
        if (ch == '.')
        {
            i++;
            string name;
            while (i < n && (isIdentChar(c[i]) || c[i] == '\\'))
            {
                if (c[i] == '\\' && i + 1 < n)
                {
                    i++;  // an escaped character stands for itself
                }
                name.push_back(c[i]);
                i++;
            }
            if (name.empty())
            {
                return std::nullopt;
            }
            out.classes.push_back(std::move(name));
        }
        else if (ch == '[')
        {
            size_t j = i + 1;
            char quote = 0;
            for (; j < n; j++)
            {
                const char d = c[j];
                if (quote != 0)
                {
                    if (d == quote)
                    {
                        quote = 0;
                    }
                    continue;
                }
                if (d == '"' || d == '\'')
                {
                    quote = d;
                }
                else if (d == ']')
                {
                    break;
                }
            }
            if (j >= n)
            {
                return std::nullopt;
            }
            auto attr = parseAttr(c.substr(i + 1, j - i - 1));
            if (!attr)
            {
                return std::nullopt;
            }
            out.attrs.push_back(std::move(*attr));
            i = j + 1;
        }
        else if (ch == ':')
        {
            size_t j = i + 1;
            if (j < n && c[j] == ':')
            {
                j++;
            }
            const size_t nameStart = j;
            while (j < n && isIdentChar(c[j]))
            {
                j++;
            }
            if (j == nameStart)
            {
                return std::nullopt;
            }
            if (j < n && c[j] == '(')
            {
                int depth = 0;
                do
                {
                    if (c[j] == '(')
                    {
                        depth++;
                    }
                    else if (c[j] == ')')
                    {
                        depth--;
                    }
                    j++;
                } while (j < n && depth > 0);
                if (depth != 0)
                {
                    return std::nullopt;
                }
            }
            out.pseudos.emplace_back(c.substr(i, j - i));
            i = j;
        }
        else
        {
            return std::nullopt;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Pseudo-classes that look at the message's elements
// ---------------------------------------------------------------------------

/// `odd`, `even`, `3`, `2n`, `-n+2`, `6n-5` as `an + b`.
std::optional<std::pair<int, int>> parseNth(string_view raw)
{
    string text;
    for (const char c : raw)
    {
        if (!isSpace(c))
        {
            text.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (text == "odd")
    {
        return std::make_pair(2, 1);
    }
    if (text == "even")
    {
        return std::make_pair(2, 0);
    }
    if (text.empty())
    {
        return std::nullopt;
    }

    const auto toInt = [](string_view part) -> std::optional<int> {
        if (part.empty())
        {
            return std::nullopt;
        }
        if (part.front() == '+')
        {
            part.remove_prefix(1);
        }
        int value = 0;
        const auto *end = part.data() + part.size();
        const auto result = std::from_chars(part.data(), end, value);
        if (result.ec != std::errc() || result.ptr != end)
        {
            return std::nullopt;
        }
        return value;
    };

    const auto n = text.find('n');
    if (n == NPOS)
    {
        if (const auto b = toInt(text))
        {
            return std::make_pair(0, *b);
        }
        return std::nullopt;
    }

    const string aText = text.substr(0, n);
    int a = 1;
    if (aText == "-")
    {
        a = -1;
    }
    else if (!aText.empty() && aText != "+")
    {
        const auto parsed = toInt(aText);
        if (!parsed)
        {
            return std::nullopt;
        }
        a = *parsed;
    }

    int b = 0;
    if (n + 1 < text.size())
    {
        const auto parsed = toInt(string_view(text).substr(n + 1));
        if (!parsed)
        {
            return std::nullopt;
        }
        b = *parsed;
    }
    return std::make_pair(a, b);
}

bool fillStructural(Compound &c, string &why, int *pseudoElement = nullptr);

std::optional<ComplexSelector> parseComplex(string_view text, string &why)
{
    ComplexSelector out;
    for (const auto &raw : tokenizeSelector(text))
    {
        auto compound = parseCompound(raw.text);
        if (!compound)
        {
            why = "unparsable selector";
            return std::nullopt;
        }
        if (!fillStructural(*compound, why))
        {
            return std::nullopt;
        }
        out.parts.push_back(SelectorPart{raw.combinator, std::move(*compound)});
    }
    if (out.parts.empty())
    {
        why = "empty selector";
        return std::nullopt;
    }
    return out;
}

/// Parses the pseudo-classes of a compound into `structural`. Returns false
/// (with `why` set) for one that isn't understood. `:hover` is left to the
/// caller. A pseudo-element is an error unless `pseudoElement` is given: then
/// `::before` / `:before` set it to 1 and `::after` / `:after` to 2 (and are
/// not otherwise looked at).
bool fillStructural(Compound &c, string &why, int *pseudoElement)
{
    using Kind = PseudoClass::Kind;

    c.structural.clear();
    for (const auto &pseudo : c.pseudos)
    {
        if (pseudo == ":hover")
        {
            continue;
        }
        if (startsWith(pseudo, "::") || pseudo == ":before" ||
            pseudo == ":after")
        {
            const string lowered = toLower(pseudo);
            if (pseudoElement != nullptr &&
                (lowered == "::before" || lowered == ":before"))
            {
                *pseudoElement = 1;
                continue;
            }
            if (pseudoElement != nullptr &&
                (lowered == "::after" || lowered == ":after"))
            {
                *pseudoElement = 2;
                continue;
            }
            why = "pseudo-element";
            return false;
        }

        const auto paren = pseudo.find('(');
        const string name = toLower(string_view(pseudo).substr(
            1, (paren == NPOS ? pseudo.size() : paren) - 1));
        string_view arg;
        if (paren != NPOS && pseudo.back() == ')')
        {
            arg = string_view(pseudo).substr(paren + 1,
                                             pseudo.size() - paren - 2);
        }

        PseudoClass pc;
        if (name == "not" || name == "is" || name == "where" || name == "has")
        {
            if (paren == NPOS)
            {
                why = "pseudo-class " + name;
                return false;
            }
            pc.kind = name == "not"   ? Kind::Not
                      : name == "has" ? Kind::Has
                                      : Kind::Is;
            for (const auto &part : splitTopLevel(arg, ','))
            {
                auto complex = parseComplex(trimView(part), why);
                if (!complex)
                {
                    return false;
                }
                if (pc.kind != Kind::Has)
                {
                    complex->parts.front().combinator = 0;
                }
                pc.args.push_back(std::move(*complex));
            }
            if (pc.args.empty())
            {
                why = "pseudo-class " + name;
                return false;
            }
        }
        else if (name == "first-child")
        {
            pc.kind = Kind::FirstChild;
        }
        else if (name == "last-child")
        {
            pc.kind = Kind::LastChild;
        }
        else if (name == "only-child")
        {
            pc.kind = Kind::OnlyChild;
        }
        else if (name == "first-of-type")
        {
            pc.kind = Kind::FirstOfType;
        }
        else if (name == "last-of-type")
        {
            pc.kind = Kind::LastOfType;
        }
        else if (name == "only-of-type")
        {
            pc.kind = Kind::OnlyOfType;
        }
        else if (name == "nth-child" || name == "nth-last-child" ||
                 name == "nth-of-type" || name == "nth-last-of-type")
        {
            const auto nth = paren == NPOS ? std::nullopt : parseNth(arg);
            if (!nth)
            {
                why = "pseudo-class " + name;
                return false;
            }
            pc.kind = name == "nth-child"        ? Kind::NthChild
                      : name == "nth-last-child" ? Kind::NthLastChild
                      : name == "nth-of-type"    ? Kind::NthOfType
                                                 : Kind::NthLastOfType;
            pc.a = nth->first;
            pc.b = nth->second;
        }
        else
        {
            why = "pseudo-class " + name;
            return false;
        }
        c.structural.push_back(std::move(pc));
    }
    return true;
}

int complexSpecificity(const ComplexSelector &complex);

/// Classes, attributes and pseudo-classes count one each; `:not()`, `:is()`
/// and `:has()` count like their most specific argument.
int compoundSpecificity(const Compound &c)
{
    int n = static_cast<int>(c.classes.size() + c.attrs.size());
    for (const auto &pseudo : c.pseudos)
    {
        if (pseudo == ":hover")
        {
            n++;
        }
    }
    for (const auto &pc : c.structural)
    {
        if (pc.args.empty())
        {
            n++;
            continue;
        }
        int best = 0;
        for (const auto &arg : pc.args)
        {
            best = std::max(best, complexSpecificity(arg));
        }
        n += best;
    }
    return n;
}

int complexSpecificity(const ComplexSelector &complex)
{
    int n = 0;
    for (const auto &part : complex.parts)
    {
        n += compoundSpecificity(part.compound);
    }
    return n;
}

struct Analysis {
    /// Empty if the selector is supported, else why not.
    string rejected;

    string emote;
    std::vector<SelectorPart> parts;
    int specificity = 0;
    bool hover = false;
    bool dependsOnContext = false;
    /// 0 for the emote itself, 1 for its `::before`, 2 for its `::after`.
    int pseudo = 0;
};

/// Checks that a selector has the shape DGG's per-emote rules have, and
/// extracts what's needed to evaluate it.
Analysis analyseSelector(string_view selector)
{
    Analysis out;
    const auto raw = tokenizeSelector(selector);
    if (raw.empty())
    {
        out.rejected = "empty selector";
        return out;
    }

    for (const auto &part : raw)
    {
        auto compound = parseCompound(part.text);
        if (!compound)
        {
            out.rejected = "unparsable selector";
            return out;
        }
        out.parts.push_back(
            SelectorPart{part.combinator, std::move(*compound)});
    }
    out.parts.front().combinator = 0;

    // The subject: `.emote.NAME` (a class can be repeated to make the rule
    // more specific)
    const auto &subject = out.parts.back().compound;
    string subjectEmote;
    bool subjectOk =
        subject.attrs.empty() && contains(subject.classes, "emote");
    for (const auto &cls : subject.classes)
    {
        if (cls == "emote")
        {
            continue;
        }
        if (subjectEmote.empty())
        {
            subjectEmote = cls;
        }
        else if (subjectEmote != cls)
        {
            subjectOk = false;
        }
    }
    if (!subjectOk || subjectEmote.empty())
    {
        out.rejected = "selector isn't for a single emote";
        return out;
    }
    out.emote = subjectEmote;

    for (size_t partIndex = 0; partIndex < out.parts.size(); partIndex++)
    {
        auto &part = out.parts[partIndex];
        string why;
        int pseudoElement = 0;
        if (!fillStructural(part.compound, why, &pseudoElement))
        {
            out.rejected = why;
            return out;
        }
        if (pseudoElement != 0)
        {
            if (partIndex + 1 != out.parts.size())
            {
                // `.emote::before + .x` and the like never match anything
                out.rejected = "pseudo-element";
                return out;
            }
            out.pseudo = pseudoElement;
        }
        for (const auto &pseudo : part.compound.pseudos)
        {
            if (pseudo == ":hover")
            {
                out.hover = true;
            }
        }
        if (!part.compound.structural.empty())
        {
            out.dependsOnContext = true;
        }
    }

    // Everything above the subject must be a part of chat-gui's message DOM
    // that we know: the message, the nick, the text, or another emote.
    for (size_t i = 0; i + 1 < out.parts.size(); i++)
    {
        const auto &c = out.parts[i].compound;
        const bool isEmote = contains(c.classes, "emote");
        const bool isGreentext = contains(c.classes, "greentext");
        const bool isMessage = contains(c.classes, "msg-chat") ||
                               contains(c.classes, "msg-emote") ||
                               (c.classes.empty() && !c.attrs.empty());
        const bool isUser = contains(c.classes, "user");
        const bool isChatUser = contains(c.classes, "chat-user");
        const bool isText = c.classes.size() == 1 && c.classes[0] == "text";

        if (isMessage && !isUser && !isText && !isEmote && !isGreentext)
        {
            for (const auto &attr : c.attrs)
            {
                // the nick's name is only compared for equality; the combo
                // attributes of a combo line take every operator
                const bool known = attr.name == "data-username"
                                       ? attr.op == "="
                                       : (attr.name == "data-combo" ||
                                          attr.name == "data-combo-group");
                if (!known)
                {
                    out.rejected = "attribute " + attr.name;
                    return out;
                }
            }
            if (contains(c.classes, "text"))
            {
                out.rejected = "selector context";
                return out;
            }
            if (!c.attrs.empty())
            {
                out.dependsOnContext = true;
            }
            for (const auto &cls : c.classes)
            {
                if (cls != "msg-chat" && cls != "msg-user")
                {
                    out.dependsOnContext = true;
                }
            }
        }
        else if (isEmote && !isMessage && !isUser && !isText && !isGreentext &&
                 c.attrs.empty())
        {
            // another emote of the message
            out.dependsOnContext = true;
        }
        else if (isUser && !isMessage && !isEmote && c.attrs.empty())
        {
            if (c.classes.size() > 1)
            {
                out.dependsOnContext = true;
            }
        }
        else if (isChatUser && !isMessage && !isEmote && !isUser &&
                 c.classes.size() == 1 && c.attrs.empty())
        {
            // a name in the text (`@nick`, or the nick of somebody in chat):
            // chat-gui makes it a `<span class="chat-user">`, a sibling of
            // the emotes (`.chat-user + .emote.NoTears`). Without this branch
            // every such rule was dropped as "selector context".
            out.dependsOnContext = true;
        }
        else if (isText && c.attrs.empty())
        {
            // always there
        }
        else if (isGreentext && c.classes.size() == 1 && c.attrs.empty())
        {
            // only there in greentext
            out.dependsOnContext = true;
        }
        else
        {
            out.rejected = "selector context";
            return out;
        }
    }

    for (size_t i = 1; i < out.parts.size(); i++)
    {
        const char comb = out.parts[i].combinator;
        if (comb == '+' ||
            (comb == '~' &&
             contains(out.parts[i - 1].compound.classes, "emote")))
        {
            out.dependsOnContext = true;
        }
    }

    for (const auto &part : out.parts)
    {
        out.specificity += compoundSpecificity(part.compound);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Matching against a chat-gui message
// ---------------------------------------------------------------------------

/// The elements of a message that selectors can talk about:
/// message (M) > [nick (U), text (T) > [greentext (G) >] emotes (E) and
/// links (L)].
struct Node {
    char kind = 0;
    /// The element name, which `:*-of-type` counts by.
    char tag = 0;
    std::vector<string> classes;
    string username;
    /// The attributes of a combo line (`data-combo`, `data-combo-group`).
    std::vector<std::pair<string, string>> attrs;
    int parent = -1;
    /// Position among the parent's children.
    int pos = 0;
};

struct Dom {
    std::vector<Node> nodes;
    std::vector<std::vector<int>> children;
    int emote = -1;
};

int addNode(Dom &dom, Node node, int parent)
{
    const int index = static_cast<int>(dom.nodes.size());
    node.parent = parent;
    if (parent >= 0)
    {
        auto &siblings = dom.children[static_cast<size_t>(parent)];
        node.pos = static_cast<int>(siblings.size());
        siblings.push_back(index);
    }
    dom.nodes.push_back(std::move(node));
    dom.children.emplace_back();
    return index;
}

Dom buildDom(const string &emote, const DggEmoteContext &ctx)
{
    Dom dom;

    Node m;
    m.kind = 'M';
    m.tag = 'd';
    if (ctx.comboMessage)
    {
        // a combo line has no author: no nick, no `data-username`
        m.classes = {"msg-chat", "msg-emote"};
        if (ctx.comboCount > 0)
        {
            m.attrs.emplace_back("data-combo", std::to_string(ctx.comboCount));
            m.attrs.emplace_back("data-combo-group",
                                 dggComboGroup(ctx.comboCount).toStdString());
        }
    }
    else
    {
        m.classes = {"msg-chat", "msg-user"};
        for (const auto &cls : ctx.messageClasses)
        {
            m.classes.push_back(cls.toStdString());
        }
        m.username = ctx.username.toStdString();
    }
    const int mIndex = addNode(dom, std::move(m), -1);

    if (!ctx.comboMessage)
    {
        Node u;
        u.kind = 'U';
        u.tag = 'a';
        u.classes = {"user"};
        if (!ctx.nickColorFlair.isEmpty())
        {
            u.classes.push_back(ctx.nickColorFlair.toStdString());
        }
        addNode(dom, std::move(u), mIndex);
    }

    Node t;
    t.kind = 'T';
    t.tag = 's';
    t.classes = {"text"};
    int container = addNode(dom, std::move(t), mIndex);

    if (ctx.greentext && !ctx.comboMessage)
    {
        Node g;
        g.kind = 'G';
        g.tag = 's';
        g.classes = {"greentext"};
        container = addNode(dom, std::move(g), container);
    }

    // The elements of the text. Without any information about the message
    // the emote is the only one.
    QStringList elements = ctx.elements;
    int index = ctx.elementIndex;
    const QString name = QString::fromStdString(emote);
    if (ctx.comboMessage || index < 0 || index >= elements.size() ||
        elements[index] != name)
    {
        elements = QStringList{name};
        index = 0;
    }
    for (int i = 0; i < elements.size(); i++)
    {
        Node e;
        if (elements[i] == DGG_CHAT_USER_ELEMENT)
        {
            e.kind = 'L';
            e.tag = 's';
            e.classes = {"chat-user"};
        }
        else if (elements[i].isEmpty())
        {
            e.kind = 'L';
            e.tag = 'a';
            e.classes = {"externallink"};
        }
        else
        {
            e.kind = 'E';
            e.tag = 'd';
            e.classes = {"emote", elements[i].toStdString()};
        }
        const int node = addNode(dom, std::move(e), container);
        if (i == index)
        {
            dom.emote = node;
        }
    }
    return dom;
}

bool isSubset(const std::vector<string> &wanted,
              const std::vector<string> &have)
{
    return std::all_of(wanted.begin(), wanted.end(), [&](const string &cls) {
        return contains(have, cls);
    });
}

bool compoundMatches(const Compound &c, const Dom &dom, int nodeIndex);
bool matchChain(const std::vector<SelectorPart> &parts, size_t index,
                const Dom &dom, int nodeIndex);

/// The nodes that share a parent with `nodeIndex` (itself included).
std::vector<int> siblingsOf(const Dom &dom, int nodeIndex)
{
    const Node &node = dom.nodes[static_cast<size_t>(nodeIndex)];
    if (node.parent < 0)
    {
        return {nodeIndex};
    }
    return dom.children[static_cast<size_t>(node.parent)];
}

bool nthMatches(int a, int b, int position)
{
    if (a == 0)
    {
        return position == b;
    }
    // 64 bit: `b` comes from the stylesheet and `position - b` can leave
    // the range of an int
    const long long diff = static_cast<long long>(position) - b;
    return diff % a == 0 && diff / a >= 0;
}

/// Whether anything matching `parts[i..]` hangs off `from` the way the
/// combinators say (the first one is the leading combinator of a `:has()`).
bool hasMatch(const std::vector<SelectorPart> &parts, size_t i, const Dom &dom,
              int from)
{
    char comb = parts[i].combinator;
    if (comb == 0)
    {
        comb = ' ';
    }

    std::vector<int> candidates;
    const Node &node = dom.nodes[static_cast<size_t>(from)];
    switch (comb)
    {
        case '+':
        case '~': {
            const auto siblings = siblingsOf(dom, from);
            const size_t pos = static_cast<size_t>(node.pos);
            if (node.parent >= 0)
            {
                if (comb == '+' && pos + 1 < siblings.size())
                {
                    candidates.push_back(siblings[pos + 1]);
                }
                else if (comb == '~')
                {
                    for (size_t k = pos + 1; k < siblings.size(); k++)
                    {
                        candidates.push_back(siblings[k]);
                    }
                }
            }
            break;
        }
        case '>':
            candidates = dom.children[static_cast<size_t>(from)];
            break;
        default: {
            std::vector<int> stack = dom.children[static_cast<size_t>(from)];
            while (!stack.empty())
            {
                const int next = stack.back();
                stack.pop_back();
                candidates.push_back(next);
                for (const int child : dom.children[static_cast<size_t>(next)])
                {
                    stack.push_back(child);
                }
            }
            break;
        }
    }

    for (const int candidate : candidates)
    {
        if (!compoundMatches(parts[i].compound, dom, candidate))
        {
            continue;
        }
        if (i + 1 == parts.size() || hasMatch(parts, i + 1, dom, candidate))
        {
            return true;
        }
    }
    return false;
}

bool evalPseudo(const PseudoClass &pc, const Dom &dom, int nodeIndex)
{
    using Kind = PseudoClass::Kind;

    switch (pc.kind)
    {
        case Kind::Not:
            return std::none_of(
                pc.args.begin(), pc.args.end(), [&](const ComplexSelector &a) {
                    return matchChain(a.parts, a.parts.size() - 1, dom,
                                      nodeIndex);
                });
        case Kind::Is:
            return std::any_of(
                pc.args.begin(), pc.args.end(), [&](const ComplexSelector &a) {
                    return matchChain(a.parts, a.parts.size() - 1, dom,
                                      nodeIndex);
                });
        case Kind::Has:
            return std::any_of(pc.args.begin(), pc.args.end(),
                               [&](const ComplexSelector &a) {
                                   return hasMatch(a.parts, 0, dom, nodeIndex);
                               });
        default:
            break;
    }

    const Node &node = dom.nodes[static_cast<size_t>(nodeIndex)];
    const auto siblings = siblingsOf(dom, nodeIndex);
    // 1-based, among all siblings. (This used to be worked out from the
    // address of the loop variable below, which is a copy of the element and
    // not the element itself: the "position" was garbage, so `:last-child`,
    // `:first-child` and `:nth-*` almost never matched. That is why the
    // second NoTears of `NoTears words NoTears` kept throwing its banana,
    // `.emote + .emote.NoTears:last-child` never matched it.)
    const int position = node.parent < 0 ? 1 : node.pos + 1;
    int count = static_cast<int>(siblings.size());
    int typedPosition = 0;
    int typedCount = 0;
    for (const int sibling : siblings)
    {
        const bool sameType =
            dom.nodes[static_cast<size_t>(sibling)].tag == node.tag;
        if (sameType)
        {
            typedCount++;
        }
        if (sibling == nodeIndex)
        {
            typedPosition = typedCount;
        }
    }

    switch (pc.kind)
    {
        case Kind::FirstChild:
            return position == 1;
        case Kind::LastChild:
            return position == count;
        case Kind::OnlyChild:
            return count == 1;
        case Kind::FirstOfType:
            return typedPosition == 1;
        case Kind::LastOfType:
            return typedPosition == typedCount;
        case Kind::OnlyOfType:
            return typedCount == 1;
        case Kind::NthChild:
            return nthMatches(pc.a, pc.b, position);
        case Kind::NthLastChild:
            return nthMatches(pc.a, pc.b, count - position + 1);
        case Kind::NthOfType:
            return nthMatches(pc.a, pc.b, typedPosition);
        case Kind::NthLastOfType:
            return nthMatches(pc.a, pc.b, typedCount - typedPosition + 1);
        default:
            return false;
    }
}

/// One attribute selector against the message element, with the operators CSS
/// has (`[a]`, `=`, `~=`, `|=`, `^=`, `$=`, `*=`).
bool messageAttrMatches(const Attr &attr, const Node &node)
{
    if (attr.name == "data-username")
    {
        // (only `=` gets here, see the analysis); a combo line has no author
        return attr.op == "=" && !node.username.empty() &&
               attr.value == node.username;
    }

    const string *value = nullptr;
    for (const auto &[name, text] : node.attrs)
    {
        if (name == attr.name)
        {
            value = &text;
            break;
        }
    }
    if (value == nullptr)
    {
        return false;
    }

    const auto &op = attr.op;
    const auto &want = attr.value;
    if (op.empty())
    {
        return true;
    }
    if (op == "=")
    {
        return *value == want;
    }
    if (want.empty())
    {
        // `^=`, `$=`, `*=` and `~=` with nothing to look for match nothing
        return op == "|=" && value->empty();
    }
    if (op == "~=")
    {
        // one of the white-space separated words
        size_t i = 0;
        while (i < value->size())
        {
            while (i < value->size() && isSpace((*value)[i]))
            {
                i++;
            }
            const size_t start = i;
            while (i < value->size() && !isSpace((*value)[i]))
            {
                i++;
            }
            if (i > start && value->compare(start, i - start, want) == 0)
            {
                return true;
            }
        }
        return false;
    }
    if (op == "|=")
    {
        return *value == want || (value->size() > want.size() &&
                                  value->compare(0, want.size(), want) == 0 &&
                                  (*value)[want.size()] == '-');
    }
    if (op == "^=")
    {
        return value->size() >= want.size() &&
               value->compare(0, want.size(), want) == 0;
    }
    if (op == "$=")
    {
        return value->size() >= want.size() &&
               value->compare(value->size() - want.size(), want.size(), want) ==
                   0;
    }
    if (op == "*=")
    {
        return value->find(want) != NPOS;
    }
    return false;
}

bool compoundMatches(const Compound &c, const Dom &dom, int nodeIndex)
{
    const Node &node = dom.nodes[static_cast<size_t>(nodeIndex)];

    bool matches = false;
    switch (node.kind)
    {
        case 'M': {
            matches = true;
            if (c.classes.empty() && c.attrs.empty())
            {
                matches = false;
            }
            else if (contains(c.classes, "user") ||
                     contains(c.classes, "text") ||
                     contains(c.classes, "emote") ||
                     contains(c.classes, "greentext"))
            {
                matches = false;
            }
            else if (!isSubset(c.classes, node.classes))
            {
                matches = false;
            }
            else
            {
                for (const auto &attr : c.attrs)
                {
                    if (!messageAttrMatches(attr, node))
                    {
                        matches = false;
                        break;
                    }
                }
            }
            break;
        }
        case 'U':
            matches = contains(c.classes, "user") && c.attrs.empty() &&
                      isSubset(c.classes, node.classes);
            break;
        case 'T':
            matches = c.classes.size() == 1 && c.classes[0] == "text" &&
                      c.attrs.empty();
            break;
        case 'G':
            matches = c.classes.size() == 1 && c.classes[0] == "greentext" &&
                      c.attrs.empty();
            break;
        case 'E':
        case 'L':
            matches = c.attrs.empty() && isSubset(c.classes, node.classes);
            break;
        default:
            break;
    }
    if (!matches)
    {
        return false;
    }

    return std::all_of(c.structural.begin(), c.structural.end(),
                       [&](const PseudoClass &pc) {
                           return evalPseudo(pc, dom, nodeIndex);
                       });
}

bool matchChain(const std::vector<SelectorPart> &parts, size_t index,
                const Dom &dom, int nodeIndex)
{
    const Node &node = dom.nodes[static_cast<size_t>(nodeIndex)];
    if (!compoundMatches(parts[index].compound, dom, nodeIndex))
    {
        return false;
    }
    if (index == 0)
    {
        return true;
    }

    switch (parts[index].combinator)
    {
        case ' ':
            for (int a = node.parent; a >= 0;
                 a = dom.nodes[static_cast<size_t>(a)].parent)
            {
                if (matchChain(parts, index - 1, dom, a))
                {
                    return true;
                }
            }
            return false;
        case '>':
            return node.parent >= 0 &&
                   matchChain(parts, index - 1, dom, node.parent);
        case '+': {
            if (node.parent < 0 || node.pos == 0)
            {
                return false;
            }
            const auto &siblings =
                dom.children[static_cast<size_t>(node.parent)];
            return matchChain(parts, index - 1, dom,
                              siblings[static_cast<size_t>(node.pos - 1)]);
        }
        case '~': {
            if (node.parent < 0)
            {
                return false;
            }
            const auto &siblings =
                dom.children[static_cast<size_t>(node.parent)];
            for (int k = 0; k < node.pos; k++)
            {
                if (matchChain(parts, index - 1, dom,
                               siblings[static_cast<size_t>(k)]))
                {
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

/// A stylesheet can make `var()` expand exponentially (a variable that names
/// the next one 20 times, 8 levels deep is 20^8 expansions), so the work is
/// bounded: past either limit the value counts as unresolvable.
struct VarBudget {
    int expansions = 0;
    size_t bytes = 0;
};
constexpr int MAX_VAR_EXPANSIONS = 2000;
constexpr size_t MAX_VAR_BYTES = 64 * 1024;

std::optional<string> resolveVarsBudgeted(string_view value, const Props &props,
                                          int depth, VarBudget &budget)
{
    if (depth > 8)
    {
        return std::nullopt;
    }

    string out;
    size_t i = 0;
    while (i < value.size())
    {
        const auto at = value.find("var(", i);
        if (at == NPOS)
        {
            out.append(value.substr(i));
            break;
        }
        out.append(value.substr(i, at - i));

        // find the matching parenthesis
        size_t j = at + 4;
        int parens = 1;
        while (j < value.size() && parens > 0)
        {
            if (value[j] == '(')
            {
                parens++;
            }
            else if (value[j] == ')')
            {
                parens--;
            }
            j++;
        }
        if (parens != 0)
        {
            return std::nullopt;
        }
        if (++budget.expansions > MAX_VAR_EXPANSIONS)
        {
            return std::nullopt;
        }

        const auto inner = value.substr(at + 4, j - 1 - (at + 4));
        const auto comma = inner.find(',');
        const auto name = trimView(inner.substr(0, comma));
        const auto found = props.find(string(name));
        std::optional<string> replacement;
        if (found != props.end())
        {
            replacement =
                resolveVarsBudgeted(found->second, props, depth + 1, budget);
        }
        else if (comma != NPOS)
        {
            replacement = resolveVarsBudgeted(trimView(inner.substr(comma + 1)),
                                              props, depth + 1, budget);
        }
        if (!replacement)
        {
            return std::nullopt;
        }
        budget.bytes += replacement->size();
        if (budget.bytes > MAX_VAR_BYTES)
        {
            return std::nullopt;
        }
        out.append(*replacement);
        i = j;
    }
    return out;
}

/// Replaces `var(--x, fallback)`. Returns nullopt if a variable is neither
/// defined nor has a fallback, or if the expansion is absurdly large.
std::optional<string> resolveVars(string_view value, const Props &props,
                                  int depth = 0)
{
    VarBudget budget;
    return resolveVarsBudgeted(value, props, depth, budget);
}

/// A time, or `calc(<time> / <number>)` / `calc(<time> * <number>)`, in ms.
std::optional<double> evalDurationMs(string_view value, const Props &props)
{
    const auto resolved = resolveVars(value, props);
    if (!resolved)
    {
        return std::nullopt;
    }
    auto text = trimView(*resolved);

    if (auto plain = parseTimeMs(text))
    {
        return plain;
    }

    if (!startsWith(text, "calc(") || text.back() != ')')
    {
        return std::nullopt;
    }
    const auto inner = trimView(text.substr(5, text.size() - 6));
    const auto op = inner.find_first_of("/*");
    if (op == NPOS)
    {
        return std::nullopt;
    }
    const auto lhs = parseTimeMs(inner.substr(0, op));
    const auto rhs = parseNumber(inner.substr(op + 1));
    if (!lhs || !rhs)
    {
        return std::nullopt;
    }
    if (inner[op] == '/')
    {
        if (*rhs == 0.0)
        {
            return std::nullopt;
        }
        return *lhs / *rhs;
    }
    return *lhs * *rhs;
}

struct Steps {
    int count = 0;
    /// `jump-end` (the default) or `jump-start`.
    bool jumpStart = false;
};

std::optional<Steps> parseSteps(string_view timing)
{
    timing = trimView(timing);
    if (timing == "step-start")
    {
        return Steps{1, true};
    }
    if (timing == "step-end")
    {
        return Steps{1, false};
    }
    if (!startsWith(timing, "steps(") || timing.back() != ')')
    {
        return std::nullopt;
    }

    const auto args = splitTopLevel(timing.substr(6, timing.size() - 7), ',');
    if (args.empty() || args.size() > 2)
    {
        return std::nullopt;
    }
    const auto count = parseNumber(args[0]);
    if (!count || *count < 1 || *count != std::floor(*count))
    {
        return std::nullopt;
    }

    Steps steps;
    steps.count = static_cast<int>(std::min(*count, 1e6));
    if (args.size() == 2)
    {
        const auto jump = toLower(trimView(args[1]));
        if (jump == "jump-start" || jump == "start")
        {
            steps.jumpStart = true;
        }
        else if (jump != "jump-end" && jump != "end")
        {
            return std::nullopt;  // jump-none and jump-both
        }
    }
    return steps;
}

/// `animation` is a shorthand: it sets every longhand, so whatever an earlier
/// rule set for the ones it doesn't mention is reset.
void expandAnimation(Props &props, string_view value)
{
    props["animation-name"] = "none";
    props["animation-duration"] = "0s";
    props["animation-timing-function"] = "ease";
    props["animation-iteration-count"] = "1";
    props["animation-fill-mode"] = "none";
    props["animation-direction"] = "normal";
    props["animation-delay"] = "0s";

    // Only one animation of a list is looked at: the first, unless a later
    // one steps through the sprite sheet and the first doesn't. KYSNOW's list
    // starts with a smooth 0.5s nudge, then has the flash, then the lightning
    // frames (`steps(49)`); looking only at the nudge left it a still picture.
    // Lists that already start with a `steps()` entry are unchanged.
    const auto entries = splitTopLevel(value, ',');
    size_t chosen = 0;
    if (toLower(entries.front()).find("steps(") == string::npos)
    {
        for (size_t i = 1; i < entries.size(); i++)
        {
            if (toLower(entries[i]).find("steps(") != string::npos)
            {
                chosen = i;
                break;
            }
        }
    }
    const auto firstRaw = entries[chosen];

    // `var(--t-NoTears)` stands for a time (or anything else), so it is
    // replaced before the entry is split into tokens; a variable that can't
    // be resolved is left out rather than taken for the animation's name
    const auto resolvedFirst = resolveVars(firstRaw, props);
    const string first = resolvedFirst ? *resolvedFirst : string(firstRaw);

    bool durationSeen = false;
    for (const auto &token : splitWhitespace(first))
    {
        const auto lower = toLower(token);
        if (lower.find("var(") != string::npos)
        {
            continue;
        }
        if (startsWith(lower, "calc(") || parseTimeMs(token))
        {
            if (!durationSeen)
            {
                props["animation-duration"] = token;
                durationSeen = true;
            }
            else
            {
                props["animation-delay"] = token;
            }
        }
        else if (startsWith(lower, "steps(") ||
                 startsWith(lower, "cubic-bezier(") || lower == "ease" ||
                 lower == "ease-in" || lower == "ease-out" ||
                 lower == "ease-in-out" || lower == "linear" ||
                 lower == "step-start" || lower == "step-end")
        {
            props["animation-timing-function"] = token;
        }
        else if (lower == "infinite" || parseNumber(token))
        {
            props["animation-iteration-count"] = token;
        }
        else if (lower == "forwards" || lower == "backwards" || lower == "both")
        {
            props["animation-fill-mode"] = lower;
        }
        else if (lower == "normal" || lower == "reverse" ||
                 lower == "alternate" || lower == "alternate-reverse")
        {
            props["animation-direction"] = lower;
        }
        else if (lower != "none")
        {
            props["animation-name"] = token;
        }
    }
}

void setProperty(Props &props, const string &name, const string &value)
{
    if (name == "animation")
    {
        expandAnimation(props, value);
        // the whole list, for the baking of transform/filter animations (the
        // expanded properties only keep the first animation)
        props["animation-raw"] = value;
        // the shorthand resets the delay a longhand set before it
        props.erase("animation-delay-override");
        props.erase("animation-direction-override");
        return;
    }
    if (name == "animation-direction" && props.count("animation-raw") != 0)
    {
        // `animation: COGGERS-anim 2.4s 1 ...;` followed by
        // `animation-direction: reverse;` (a `:nth-child(even)` rule) keeps
        // the list and only changes the direction, which is applied to the
        // parsed list (see parseAnimationsOf())
        props["animation-direction-override"] = value;
        props[name] = value;
        return;
    }
    if (name == "animation-delay" && props.count("animation-raw") != 0)
    {
        // `animation: GSN-anim 1s 1; animation-delay: 0.25s;` keeps the list
        // and only changes when each animation starts, which is applied to
        // the parsed list (see parseAnimationsOf())
        props["animation-delay-override"] = value;
        props[name] = value;
        return;
    }
    if (name.rfind("animation-", 0) == 0 && name != "animation-raw" &&
        name != "animation-delay-override" &&
        name != "animation-direction-override")
    {
        // a longhand changes what the shorthand said, so the raw list is no
        // longer what applies
        props.erase("animation-raw");
        props.erase("animation-delay-override");
        props.erase("animation-direction-override");
    }

    if (name == "background-position")
    {
        const auto tokens = splitWhitespace(value);
        const auto isVertical = [](const string &t) {
            return t == "top" || t == "bottom";
        };
        const auto isHorizontal = [](const string &t) {
            return t == "left" || t == "right";
        };

        if (tokens.size() == 1)
        {
            // the missing one defaults to `center`
            if (isVertical(tokens[0]))
            {
                props["background-position-x"] = "50%";
                props["background-position-y"] = tokens[0];
            }
            else
            {
                props["background-position-x"] = tokens[0];
                props["background-position-y"] = "50%";
            }
        }
        else if (tokens.size() == 2)
        {
            if (isVertical(tokens[0]) || isHorizontal(tokens[1]))
            {
                props["background-position-x"] = tokens[1];
                props["background-position-y"] = tokens[0];
            }
            else
            {
                props["background-position-x"] = tokens[0];
                props["background-position-y"] = tokens[1];
            }
        }
        else
        {
            // edge offsets (`right 4px top 2px`) aren't used by DGG
            props["background-position-x"] = "unsupported";
            props["background-position-y"] = "unsupported";
        }
        return;
    }

    props[name] = value;
}

/// The value of `background-position` along one axis in pixels, as CSS
/// computes it: how far the image is moved right/down from its default
/// position. Keywords and percentages are relative to the free space,
/// `frame - sheet`.
std::optional<double> positionPx(string_view raw, const Props &props,
                                 double frame, double sheet)
{
    const auto resolved = resolveVars(raw, props);
    if (!resolved)
    {
        return std::nullopt;
    }
    const auto value = toLower(trimView(*resolved));

    double fraction = -1;
    if (value == "left" || value == "top")
    {
        fraction = 0;
    }
    else if (value == "center")
    {
        fraction = 0.5;
    }
    else if (value == "right" || value == "bottom")
    {
        fraction = 1;
    }
    else if (!value.empty() && value.back() == '%')
    {
        const auto percent =
            parseNumber(string_view(value).substr(0, value.size() - 1));
        if (!percent)
        {
            return std::nullopt;
        }
        fraction = *percent / 100.0;
    }

    if (fraction >= 0 || (!value.empty() && value.back() == '%'))
    {
        if (sheet <= 0)
        {
            return 0.0;  // size of the image not known
        }
        return (frame - sheet) * fraction;
    }
    return parseLengthPx(value);
}

/// `SpriteColorFilter` <-> the animation engine's `FilterFn` (the kinds are
/// numbered alike, see the static_assert below).
SpriteColorFilter toSpriteFilter(const dggcss::anim::FilterFn &fn)
{
    return SpriteColorFilter{
        .kind = static_cast<SpriteColorFilter::Kind>(fn.kind),
        .amount = fn.amount,
        .shadowX = fn.x,
        .shadowY = fn.y,
        .shadowBlur = fn.blur,
        .shadowRgba = fn.rgba,
    };
}

dggcss::anim::FilterFn toAnimFilter(const SpriteColorFilter &filter)
{
    return dggcss::anim::FilterFn{
        .kind = static_cast<dggcss::anim::FilterKind>(filter.kind),
        .amount = filter.amount,
        .x = filter.shadowX,
        .y = filter.shadowY,
        .blur = filter.shadowBlur,
        .rgba = filter.shadowRgba,
    };
}

/// A glow reaches this far out of an emote at most (px).
constexpr int MAX_GLOW_MARGIN = 12;

void parseFilters(string_view raw, const Props &props,
                  std::vector<SpriteColorFilter> &out,
                  std::vector<string> &ignored)
{
    using Kind = SpriteColorFilter::Kind;

    const auto resolved = resolveVars(raw, props);
    if (!resolved)
    {
        ignored.emplace_back("filter");
        return;
    }
    const string_view value = trimView(*resolved);
    if (value == "none")
    {
        return;
    }

    size_t i = 0;
    while (i < value.size())
    {
        while (i < value.size() && isSpace(value[i]))
        {
            i++;
        }
        if (i >= value.size())
        {
            break;
        }

        const auto open = value.find('(', i);
        if (open == NPOS)
        {
            ignored.emplace_back("filter");
            return;
        }
        int depth = 0;
        size_t close = open;
        for (; close < value.size(); close++)
        {
            if (value[close] == '(')
            {
                depth++;
            }
            else if (value[close] == ')' && --depth == 0)
            {
                break;
            }
        }
        if (close >= value.size())
        {
            ignored.emplace_back("filter");
            return;
        }

        const auto name = toLower(trimView(value.substr(i, open - i)));
        const auto args = trimView(value.substr(open + 1, close - open - 1));
        i = close + 1;

        SpriteColorFilter filter;
        bool known = true;
        if (name == "brightness")
        {
            filter.kind = Kind::Brightness;
        }
        else if (name == "contrast")
        {
            filter.kind = Kind::Contrast;
        }
        else if (name == "saturate")
        {
            filter.kind = Kind::Saturate;
        }
        else if (name == "grayscale")
        {
            filter.kind = Kind::Grayscale;
        }
        else if (name == "sepia")
        {
            filter.kind = Kind::Sepia;
        }
        else if (name == "invert")
        {
            filter.kind = Kind::Invert;
        }
        else if (name == "opacity")
        {
            filter.kind = Kind::Opacity;
        }
        else if (name == "hue-rotate")
        {
            filter.kind = Kind::HueRotate;
        }
        else if (name == "drop-shadow")
        {
            const auto parsed = dggcss::anim::parseDeclaration(
                "filter", "drop-shadow(" + string(args) + ")");
            const auto *list =
                parsed ? std::get_if<dggcss::anim::FilterList>(&parsed->second)
                       : nullptr;
            if (list == nullptr || list->size() != 1)
            {
                ignored.emplace_back("filter drop-shadow");
                continue;
            }
            out.push_back(toSpriteFilter(list->front()));
            continue;
        }
        else
        {
            known = false;
        }
        if (!known)
        {
            ignored.push_back("filter " + name);
            continue;
        }

        if (filter.kind == Kind::HueRotate)
        {
            filter.amount = 0;
            if (!args.empty())
            {
                size_t pos = 0;
                const auto number = parseNumberAt(args, pos);
                if (!number)
                {
                    ignored.push_back("filter " + name);
                    continue;
                }
                const auto unit = args.substr(pos);
                if (unit == "deg" || (unit.empty() && *number == 0.0))
                {
                    filter.amount = *number;
                }
                else if (unit == "rad")
                {
                    filter.amount = *number * 180.0 / 3.14159265358979323846;
                }
                else if (unit == "grad")
                {
                    filter.amount = *number * 0.9;
                }
                else if (unit == "turn")
                {
                    filter.amount = *number * 360.0;
                }
                else
                {
                    ignored.push_back("filter " + name);
                    continue;
                }
            }
        }
        else if (!args.empty())
        {
            size_t pos = 0;
            const auto number = parseNumberAt(args, pos);
            const auto unit = args.substr(pos);
            if (!number || (!unit.empty() && unit != "%"))
            {
                ignored.push_back("filter " + name);
                continue;
            }
            filter.amount = unit == "%" ? *number / 100.0 : *number;
        }
        out.push_back(filter);
    }
}

/// The `background-position` x value of a keyframe stop, if it has one.
/// `found` says whether the stop sets one at all, the result whether it could
/// be evaluated.
std::optional<double> keyframePositionX(const std::vector<KeyframeStop> &stops,
                                        double percent, const Props &props,
                                        double frame, double sheet, bool &found)
{
    found = false;
    string raw;
    for (const auto &stop : stops)
    {
        if (stop.percent != percent)
        {
            continue;
        }
        for (const auto &decl : stop.decls)
        {
            if (decl.name == "background-position")
            {
                const auto tokens = splitWhitespace(decl.value);
                if (!tokens.empty())
                {
                    raw = tokens[0];
                    found = true;
                }
            }
            else if (decl.name == "background-position-x")
            {
                raw = decl.value;
                found = true;
            }
        }
    }
    if (!found)
    {
        return std::nullopt;
    }
    return positionPx(raw, props, frame, sheet);
}

/// Evaluates the final property values of one emote.
namespace anim = dggcss::anim;

static_assert(static_cast<int>(anim::FilterKind::Brightness) ==
                      static_cast<int>(SpriteColorFilter::Kind::Brightness) &&
                  static_cast<int>(anim::FilterKind::Contrast) ==
                      static_cast<int>(SpriteColorFilter::Kind::Contrast) &&
                  static_cast<int>(anim::FilterKind::Saturate) ==
                      static_cast<int>(SpriteColorFilter::Kind::Saturate) &&
                  static_cast<int>(anim::FilterKind::HueRotate) ==
                      static_cast<int>(SpriteColorFilter::Kind::HueRotate) &&
                  static_cast<int>(anim::FilterKind::Grayscale) ==
                      static_cast<int>(SpriteColorFilter::Kind::Grayscale) &&
                  static_cast<int>(anim::FilterKind::Sepia) ==
                      static_cast<int>(SpriteColorFilter::Kind::Sepia) &&
                  static_cast<int>(anim::FilterKind::Invert) ==
                      static_cast<int>(SpriteColorFilter::Kind::Invert) &&
                  static_cast<int>(anim::FilterKind::Opacity) ==
                      static_cast<int>(SpriteColorFilter::Kind::Opacity) &&
                  static_cast<int>(anim::FilterKind::DropShadow) ==
                      static_cast<int>(SpriteColorFilter::Kind::DropShadow),
              "anim::FilterKind and SpriteColorFilter::Kind must match");

SpritePose poseOf(const anim::Computed &state, const anim::Box &box)
{
    SpritePose pose;
    const auto m = anim::matrixOf(state, box);
    pose.matrix = {m.a, m.b, m.c, m.d, m.e, m.f};
    pose.opacity = state.opacity;
    for (const auto &fn : state.filter)
    {
        pose.filters.push_back(toSpriteFilter(fn));
    }
    pose.clip = anim::clipPx(state.clip, box);
    return pose;
}

/// `background-position: x y` of a keyframe stop as the `translate` of the
/// image, relative to where the element's own `background-position` puts it
/// (`basePx`). A background that moves inside its box and a picture that is
/// shifted inside a box of the same size look the same, since both are cut
/// off at the box. Used for emotes that slide their picture in (GIGACHAD) or
/// nudge it (Sippy) with a smooth timing; the sprite code only does `steps()`.
std::optional<string> backgroundPositionAsTranslate(string_view value,
                                                    const Props &props,
                                                    QPoint basePx, QSize frame)
{
    const auto tokens = splitWhitespace(value);
    if (tokens.empty() || tokens.size() > 2)
    {
        return std::nullopt;
    }
    // a single value is the horizontal one, the vertical one is `center`
    const auto x = positionPx(tokens[0], props, frame.width(), frame.width());
    const auto y =
        tokens.size() == 2
            ? positionPx(tokens[1], props, frame.height(), frame.height())
            : std::optional<double>(0.0);
    if (!x || !y)
    {
        return std::nullopt;
    }
    char buffer[96];
    std::snprintf(buffer, sizeof buffer, "%.4gpx %.4gpx", *x + basePx.x(),
                  *y + basePx.y());
    return string(buffer);
}

/// Animations of `transform`, `opacity`, `filter` and `clip-path` can't run
/// in Chatterino, so they are baked: the keyframes are evaluated at a series
/// of moments, and every moment becomes one frame of the emote (a "pose" on
/// top of the frame the sheet shows). Only done if the emote has no sprite
/// animation of its own; the two aren't combined.
///
/// Leaves `style` alone (adding a note) if the animations can't be baked.
/// Properties that only mean something for the box of a `::before` /
/// `::after` (applyPseudoElements()). The poses of an emote can't show them.
constexpr std::array<anim::Prop, 11> BOX_PROPS{
    anim::Prop::Left,        anim::Prop::Right,      anim::Prop::Top,
    anim::Prop::Bottom,      anim::Prop::Width,      anim::Prop::Height,
    anim::Prop::FontSize,    anim::Prop::LineHeight, anim::Prop::LetterSpacing,
    anim::Prop::ContentText, anim::Prop::BoxShadow,
};

/// `anim::parseAnimationList()` plus the `animation-delay` longhand that
/// followed the shorthand in the same rule (it applies to every animation of
/// the list)
std::optional<std::vector<anim::AnimationSpec>> parseAnimationsOf(
    const Props &props, const string &rawList)
{
    auto specs = anim::parseAnimationList(rawList);
    if (!specs)
    {
        return specs;
    }
    if (const auto dir = props.find("animation-direction-override");
        dir != props.end())
    {
        const auto resolved = resolveVars(dir->second, props);
        const string text = resolved ? *resolved : dir->second;
        const auto entries = splitTopLevel(text, ',');
        for (size_t i = 0; !entries.empty() && i < specs->size(); i++)
        {
            const string entry = toLower(trimView(entries[i % entries.size()]));
            using D = anim::AnimationSpec::Direction;
            if (entry == "normal")
            {
                (*specs)[i].direction = D::Normal;
            }
            else if (entry == "reverse")
            {
                (*specs)[i].direction = D::Reverse;
            }
            else if (entry == "alternate")
            {
                (*specs)[i].direction = D::Alternate;
            }
            else if (entry == "alternate-reverse")
            {
                (*specs)[i].direction = D::AlternateReverse;
            }
        }
    }
    const auto it = props.find("animation-delay-override");
    if (it != props.end())
    {
        const auto resolved = resolveVars(it->second, props);
        const string text = resolved ? *resolved : it->second;
        const auto entries = splitTopLevel(text, ',');
        for (size_t i = 0; !entries.empty() && i < specs->size(); i++)
        {
            // a short list repeats
            const auto entry = trimView(entries[i % entries.size()]);
            if (const auto ms = parseTimeMs(entry))
            {
                (*specs)[i].delayMs = *ms;
            }
        }
    }
    return specs;
}

/// The longest stretch that is laid out when a sprite animation and a baked
/// one are combined (the same as the limit of the baking itself).
constexpr long long MAX_COMBINED_MS = 10000;

/// Frames shorter than this are dropped by the animation clock (see
/// sliceSpriteSheet()), so moments that close together are one.
constexpr long long MIN_COMBINED_FRAME_MS = 20;

/// An emote can step through the frames of its sheet and have its
/// `transform` animated at the same time, each on its own clock: BONK hits
/// with `BONK-anim` (`background-position`, see computeStyle()) while
/// `YUMP-anim` shakes it. The frames of the sheet and the baked poses are
/// laid on one timeline that is cut at every moment either of them changes,
/// and every piece of it shows the frame and the pose of that moment.
///
/// Only for two animations that both end or both go on forever (the latter
/// for as long as it takes until they line up again): a stretch that plays
/// once and is followed by one that repeats can't be written as a list of
/// frames that is played in a loop.
///
/// Leaves `style` alone (adding a note) if the two can't be combined.
bool combineWithSprite(DggEmoteStyle &style, const anim::Baked &baked,
                       const anim::Box &box, std::vector<string> &notes)
{
    auto &sprite = style.sprite;
    const size_t spriteCount = sprite.offsets.size();
    const size_t poseCount = baked.frames.size();
    if (spriteCount < 2 || sprite.durationsMs.size() != spriteCount ||
        poseCount == 0)
    {
        return false;
    }

    long long spriteMs = 0;
    for (const int ms : sprite.durationsMs)
    {
        spriteMs += std::max(ms, 0);
    }
    long long poseMs = 0;
    for (const auto &frame : baked.frames)
    {
        poseMs += std::max(frame.durationMs, 0);
    }
    if (spriteMs <= 0 || poseMs <= 0)
    {
        return false;
    }

    const bool spriteForever = sprite.loopCount == 0;
    const bool poseForever = baked.plays == 0;
    if (spriteForever != poseForever)
    {
        notes.emplace_back(
            "animation: a sprite animation and a transform animation, only "
            "one of which ends, are not combined");
        return false;
    }

    // when each of them is over, and how long the combination is
    long long spriteEnd = 0;
    long long poseEnd = 0;
    long long total = 0;
    if (spriteForever)
    {
        total = std::lcm(spriteMs, poseMs);
        spriteEnd = total;
        poseEnd = total;
    }
    else
    {
        spriteEnd = spriteMs * sprite.loopCount;
        poseEnd = poseMs * baked.plays;
        total = std::max(spriteEnd, poseEnd);
    }
    if (total > MAX_COMBINED_MS)
    {
        notes.emplace_back(
            "animation: a sprite animation and a transform animation that "
            "take more than 10 s together are not combined");
        return false;
    }

    // every moment one of them changes
    std::vector<long long> cuts{0};
    const auto addCuts = [&](size_t count, auto durationOf, long long period,
                             long long end) {
        for (long long start = 0; start < end; start += period)
        {
            long long at = start;
            for (size_t i = 0; i < count; i++)
            {
                at += std::max(durationOf(i), 0);
                if (at < end)
                {
                    cuts.push_back(at);
                }
            }
        }
        if (end < total)
        {
            cuts.push_back(end);
        }
    };
    addCuts(
        spriteCount,
        [&](size_t i) {
            return sprite.durationsMs[i];
        },
        spriteMs, spriteEnd);
    addCuts(
        poseCount,
        [&](size_t i) {
            return baked.frames[i].durationMs;
        },
        poseMs, poseEnd);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

    std::vector<long long> kept{0};
    for (const long long cut : cuts)
    {
        if (cut - kept.back() >= MIN_COMBINED_FRAME_MS &&
            total - cut >= MIN_COMBINED_FRAME_MS)
        {
            kept.push_back(cut);
        }
    }
    kept.push_back(total);

    // which frame of a list is shown at a moment (`count` if it is over)
    const auto locate = [](size_t count, auto durationOf, long long period,
                           long long end, long long at) -> size_t {
        if (at >= end)
        {
            return count;
        }
        long long into = at % period;
        for (size_t i = 0; i < count; i++)
        {
            into -= std::max(durationOf(i), 0);
            if (into < 0)
            {
                return i;
            }
        }
        return count - 1;
    };

    std::vector<QPoint> offsets;
    std::vector<int> durations;
    std::vector<SpritePose> poses;
    const SpritePose restPose = poseOf(baked.rest, box);
    for (size_t i = 0; i + 1 < kept.size(); i++)
    {
        const long long at = kept[i];
        const size_t spriteIndex = locate(
            spriteCount,
            [&](size_t k) {
                return sprite.durationsMs[k];
            },
            spriteMs, spriteEnd, at);
        const size_t poseIndex = locate(
            poseCount,
            [&](size_t k) {
                return baked.frames[k].durationMs;
            },
            poseMs, poseEnd, at);

        const QPoint offset = spriteIndex < spriteCount
                                  ? sprite.offsets[spriteIndex]
                                  : sprite.restOffset;
        const SpritePose pose = poseIndex < poseCount
                                    ? poseOf(baked.frames[poseIndex].state, box)
                                    : restPose;
        const int duration = static_cast<int>(kept[i + 1] - kept[i]);

        if (!poses.empty() && offsets.back() == offset && poses.back() == pose)
        {
            durations.back() += duration;
            continue;
        }
        offsets.push_back(offset);
        durations.push_back(duration);
        poses.push_back(pose);
    }

    sprite.offsets = std::move(offsets);
    sprite.durationsMs = std::move(durations);
    sprite.poses = std::move(poses);
    // the poses carry the element's filters
    sprite.filters.clear();
    // played once, and then it rests (`restOffset` is where the sheet
    // rests), or for as long as both go on
    sprite.loopCount = spriteForever ? 0 : 1;
    sprite.restPose = restPose;
    return true;
}

void bakeAnimations(const Props &props, const Keyframes &keyframes,
                    DggEmoteStyle &style, QSize sheetSize,
                    std::vector<string> &outNotes)
{
    const auto rawIt = props.find("animation-raw");
    if (rawIt == props.end() || style.sprite.offsets.empty())
    {
        return;
    }

    // An emote that steps through a sheet (BONK) is only given the poses of
    // its other animations (see combineWithSprite()). What it has to say
    // about the animations it can't use is left out, as it was before.
    const bool spriteAnimated = style.sprite.offsets.size() > 1;
    std::vector<string> localNotes;
    auto &notes = spriteAnimated ? localNotes : outNotes;
    string spriteName;
    if (spriteAnimated)
    {
        if (const auto it = props.find("animation-name"); it != props.end())
        {
            spriteName = it->second;
        }
    }
    const auto rawList = resolveVars(rawIt->second, props);
    if (!rawList || trimView(*rawList) == "none")
    {
        return;
    }
    const auto specs = parseAnimationsOf(props, *rawList);
    if (!specs)
    {
        notes.push_back("animation (not understood): " + *rawList);
        return;
    }

    // `style.sprite.offsets.front()` is the negated `background-position`
    const QPoint offset = style.sprite.offsets.front();
    std::vector<string> approximations;

    std::vector<anim::Animation> animations;
    for (const auto &spec : *specs)
    {
        if (spec.name.empty() || spec.name == "none" ||
            (spriteAnimated && spec.name == spriteName))
        {
            continue;
        }
        const auto found = keyframes.find(spec.name);
        if (found == keyframes.end())
        {
            notes.push_back("animation " + spec.name + ": no @keyframes");
            continue;
        }

        std::vector<anim::Keyframe> frames;
        // `left` / `top` of the stops (the emote nudges itself: OverRustle's
        // shake), and whether all of them can be played as a small shift
        struct Nudge {
            std::optional<double> x;
            std::optional<double> y;
        };
        std::vector<Nudge> nudges;
        bool nudgeable = true;
        bool nudged = false;
        for (const auto &stop : found->second)
        {
            anim::Keyframe frame;
            frame.offset = stop.percent / 100.0;
            Nudge nudge;
            for (const auto &decl : stop.decls)
            {
                const auto value = resolveVars(decl.value, props);
                string text = value ? *value : decl.value;
                string name = decl.name;
                if (const auto lower = toLower(name);
                    lower == "left" || lower == "top")
                {
                    // the element's own `left` / `top` is where the nudge
                    // starts from
                    const auto it = props.find(lower);
                    const auto baseText = it != props.end()
                                              ? resolveVars(it->second, props)
                                              : std::nullopt;
                    const auto base =
                        baseText ? parseLengthPx(*baseText) : std::nullopt;
                    const auto px = parseLengthPx(text);
                    const double limit =
                        0.25 * (lower == "left" ? style.frameSize.width()
                                                : style.frameSize.height());
                    if (!px || std::abs(*px - base.value_or(0.0)) > limit)
                    {
                        // a slide (GIGASIP comes in from 29 px to the left,
                        // YEEHAW rides across): a box that moves, not a nudge
                        nudgeable = false;
                    }
                    else
                    {
                        (lower == "left" ? nudge.x : nudge.y) =
                            *px - base.value_or(0.0);
                        nudged = true;
                    }
                }
                if (name == "translate" || name == "background-position")
                {
                    // already a shift of its own
                    nudgeable = false;
                }
                if (name == "background-position" &&
                    sheetSize == style.frameSize)
                {
                    // (a picture that is the whole image; a frame of a bigger
                    // sprite sheet can't be moved this way)
                    // the element's own position is `offset` negated, which
                    // is where a keyframe without one starts from
                    if (const auto shifted = backgroundPositionAsTranslate(
                            text, props, offset, style.frameSize))
                    {
                        name = "translate";
                        text = *shifted;
                        approximations.push_back(
                            "animation " + spec.name +
                            ": background-position played as a shift of the "
                            "picture");
                    }
                }
                frame.decls.push_back(anim::KeyframeDecl{name, text});
            }
            frames.push_back(std::move(frame));
            nudges.push_back(nudge);
        }

        if (nudged && nudgeable)
        {
            // `left` / `top` of a positioned emote shift the whole picture,
            // which a pose can do as a `translate` (the part pushed out of
            // the box is cut off, as with every shift here)
            for (size_t i = 0; i < frames.size(); i++)
            {
                std::erase_if(frames[i].decls, [](const anim::KeyframeDecl &d) {
                    const auto lower = toLower(d.name);
                    return lower == "left" || lower == "top";
                });
                if (nudges[i].x || nudges[i].y)
                {
                    char buffer[96];
                    std::snprintf(buffer, sizeof buffer, "%.4gpx %.4gpx",
                                  nudges[i].x.value_or(0.0),
                                  nudges[i].y.value_or(0.0));
                    frames[i].decls.push_back(
                        anim::KeyframeDecl{"translate", buffer});
                }
            }
            approximations.push_back("animation " + spec.name +
                                     ": left / top played as a shift of the "
                                     "picture");
        }

        string problem;
        auto built = anim::Animation::build(spec, frames, problem);
        if (!built)
        {
            // e.g. the `background-position` animation of a sprite sheet:
            // not this path's business
            if (problem.rfind("keyframe property background-position", 0) != 0)
            {
                notes.push_back("animation " + spec.name + ": " + problem);
            }
            continue;
        }
        if (std::any_of(BOX_PROPS.begin(), BOX_PROPS.end(),
                        [&](anim::Prop prop) {
                            return built->animates(prop);
                        }))
        {
            // (`left` of GIGASIP, `width` of NoTears: the emote's box moves,
            // which the picture can't show)
            notes.push_back(
                "animation " + spec.name +
                ": keyframe property of the box (left, width, ...)");
            continue;
        }
        animations.push_back(std::move(*built));
    }
    if (animations.empty())
    {
        return;
    }

    // What the element looks like when nothing is animated
    anim::Computed base;
    for (const char *name : {"transform", "translate", "rotate", "scale",
                             "opacity", "transform-origin", "clip-path"})
    {
        const auto it = props.find(name);
        if (it == props.end())
        {
            continue;
        }
        const auto resolved = resolveVars(it->second, props);
        if (!resolved)
        {
            continue;
        }
        if (auto parsed = anim::parseDeclaration(name, *resolved))
        {
            anim::setProp(base, parsed->first, std::move(parsed->second));
        }
    }
    for (const auto &filter : style.sprite.filters)
    {
        base.filter.push_back(toAnimFilter(filter));
    }

    const anim::Box box{
        .width = static_cast<double>(style.frameSize.width()),
        .height = static_cast<double>(style.frameSize.height()),
    };
    string problem;
    const auto baked =
        anim::bake(animations, base, box, anim::BakeOptions{}, problem);
    if (!baked)
    {
        notes.push_back("animation baking: " + problem);
        return;
    }
    if (spriteAnimated && !combineWithSprite(style, *baked, box, outNotes))
    {
        return;
    }
    if (!spriteAnimated)
    {
        // what the sprite path had to say about these animations is moot now
        std::erase_if(outNotes, [](const string &note) {
            return note.rfind("animation ", 0) == 0;
        });
    }
    std::sort(approximations.begin(), approximations.end());
    approximations.erase(
        std::unique(approximations.begin(), approximations.end()),
        approximations.end());
    for (const auto &note : approximations)
    {
        outNotes.push_back(note);
    }
    if (baked->truncated)
    {
        outNotes.emplace_back("animation baking: cut off after 10 s");
    }
    if (spriteAnimated)
    {
        return;
    }

    auto frames = baked->frames;
    if (frames.size() == 1)
    {
        // a limited animation that only has one distinct moment before it
        // rests: the second frame is what makes it count as an animation
        frames.push_back(frames.front());
    }

    style.sprite.offsets.assign(frames.size(), offset);
    style.sprite.durationsMs.clear();
    style.sprite.poses.clear();
    for (const auto &frame : frames)
    {
        style.sprite.durationsMs.push_back(frame.durationMs);
        style.sprite.poses.push_back(poseOf(frame.state, box));
    }
    // the poses carry the element's filters
    style.sprite.filters.clear();

    style.sprite.loopCount = baked->plays;
    style.sprite.restOffset = offset;
    style.sprite.restPose = poseOf(baked->rest, box);
}

/// `transform` without its `translate*()` functions: a shift would push part
/// of the frame out of its box, where it is cut off (chat-gui lets it
/// overflow instead).
string withoutTranslations(string_view transform)
{
    string out;
    size_t i = 0;
    while (i < transform.size())
    {
        if (transform.compare(i, 9, "translate") == 0)
        {
            const size_t open = transform.find('(', i);
            if (open != string_view::npos)
            {
                int depth = 0;
                size_t j = open;
                for (; j < transform.size(); j++)
                {
                    if (transform[j] == '(')
                    {
                        depth++;
                    }
                    else if (transform[j] == ')' && --depth == 0)
                    {
                        break;
                    }
                }
                i = j + 1;
                continue;
            }
        }
        out.push_back(transform[i]);
        i++;
    }
    return out;
}

/// An emote that steps through the frames of a sprite sheet can't have its
/// `transform` animated as well (see bakeAnimations()), but a plain
/// `transform` that stays the same, such as the `scale(-1, 1)` that makes one
/// of two neighbouring Chatting emotes face the other, can be drawn on every
/// frame.
void applyStaticTransform(const Props &props, DggEmoteStyle &style,
                          std::vector<string> &notes)
{
    // (also for a single frame: RapThis next to another emote is a still or
    // looping image that has to face the other way)
    if (style.sprite.hasPoses())
    {
        return;
    }
    const auto it = props.find("transform");
    if (it == props.end())
    {
        return;
    }
    const auto resolved = resolveVars(it->second, props);
    if (!resolved)
    {
        return;
    }
    const string text = withoutTranslations(*resolved);
    if (trimView(text).empty() || trimView(text) == "none")
    {
        return;
    }

    anim::Computed base;
    if (auto parsed = anim::parseDeclaration("transform", text))
    {
        anim::setProp(base, parsed->first, std::move(parsed->second));
    }
    else
    {
        notes.push_back("transform (not understood): " + *resolved);
        return;
    }
    if (const auto origin = props.find("transform-origin");
        origin != props.end())
    {
        if (const auto value = resolveVars(origin->second, props))
        {
            if (auto parsed =
                    anim::parseDeclaration("transform-origin", *value))
            {
                anim::setProp(base, parsed->first, std::move(parsed->second));
            }
        }
    }
    for (const auto &filter : style.sprite.filters)
    {
        base.filter.push_back(toAnimFilter(filter));
    }

    const anim::Box box{
        .width = static_cast<double>(style.frameSize.width()),
        .height = static_cast<double>(style.frameSize.height()),
    };
    const SpritePose pose = poseOf(base, box);
    if (pose.isIdentity())
    {
        return;
    }

    style.sprite.poses.assign(style.sprite.offsets.size(), pose);
    style.sprite.restPose = pose;
    // the pose carries the element's filters
    style.sprite.filters.clear();
}

DggEmoteStyle computeStyle(const Props &props, const Keyframes &keyframes,
                           QSize sheetSize)
{
    DggEmoteStyle style;
    std::vector<string> notes;

    const auto get = [&](const char *name) -> const string * {
        const auto it = props.find(name);
        return it == props.end() ? nullptr : &it->second;
    };
    const auto length = [&](const char *name) -> std::optional<double> {
        const auto *raw = get(name);
        if (raw == nullptr)
        {
            return std::nullopt;
        }
        const auto resolved = resolveVars(*raw, props);
        if (!resolved)
        {
            return std::nullopt;
        }
        return parseLengthPx(*resolved);
    };

    const auto width = length("width");
    const auto height = length("height");
    if (!width || !height || *width < 1 || *height < 1)
    {
        return style;  // not valid: no size
    }

    // `background-size: 32px 32px` (or one value for both) shrinks or
    // stretches the whole image, e.g. a 500px GIF drawn at 32px (BlueWave).
    if (const auto *raw = get("background-size"))
    {
        if (const auto resolved = resolveVars(*raw, props))
        {
            const string_view text = trimView(*resolved);
            const auto space = text.find(' ');
            const auto first = parseLengthPx(text.substr(0, space));
            // one value is the width; the height is `auto` (the proportions
            // of the image). Same as a square size for a square image.
            std::optional<double> second;
            if (space != string_view::npos)
            {
                const auto rest = trimView(text.substr(space + 1));
                second = rest == "auto" ? first : parseLengthPx(rest);
            }
            else if (first && !sheetSize.isEmpty())
            {
                second = *first * sheetSize.height() / sheetSize.width();
            }
            else
            {
                second = first;
            }
            if (first && second && *first >= 1 && *second >= 1)
            {
                style.backgroundSize =
                    QSize(static_cast<int>(std::lround(*first)),
                          static_cast<int>(std::lround(*second)));
            }
            else if (text != "auto" && text != "initial")
            {
                notes.emplace_back("background-size " + string(text));
            }
        }
    }
    // `background-position` is measured against the image as it is drawn: with
    // `background-size`, that is the scaled image, not the file. BlueWave
    // (500px GIF at 32px, `background-position: center`) needs this, or the
    // middle 32px of the big image is shown instead of the whole picture.
    const QSize drawnSheet =
        style.backgroundSize.isEmpty() ? sheetSize : style.backgroundSize;
    const double sheetW = drawnSheet.width();
    const double sheetH = drawnSheet.height();

    // where the static image sits
    double x0 = 0;
    double y0 = 0;
    if (const auto *rawX = get("background-position-x"))
    {
        if (const auto x = positionPx(*rawX, props, *width, sheetW))
        {
            x0 = *x;
        }
        else
        {
            notes.emplace_back("background-position");
        }
    }
    if (const auto *rawY = get("background-position-y"))
    {
        if (const auto y = positionPx(*rawY, props, *height, sheetH))
        {
            y0 = *y;
        }
        else
        {
            notes.emplace_back("background-position");
        }
    }

    // The sprite animation, if any
    std::vector<double> xs;
    std::vector<int> durations;
    // where the last keyframe leaves the background (`animation-fill-mode:
    // forwards` keeps it there)
    double endX = x0;

    const auto *nameValue = get("animation-name");
    if (nameValue != nullptr && *nameValue != "none")
    {
        const string &name = *nameValue;
        const auto found = keyframes.find(name);

        const auto *rawDuration = get("animation-duration");
        const auto duration = rawDuration != nullptr
                                  ? evalDurationMs(*rawDuration, props)
                                  : std::nullopt;
        const auto *rawTiming = get("animation-timing-function");
        const auto timing = rawTiming != nullptr
                                ? resolveVars(*rawTiming, props)
                                : std::nullopt;
        const auto steps = timing ? parseSteps(*timing) : std::nullopt;

        if (found == keyframes.end())
        {
            notes.push_back("animation " + name + ": no @keyframes");
        }
        else if (!steps)
        {
            notes.push_back("animation " + name + ": timing " +
                            (timing ? *timing : string("?")));
        }
        else if (!duration || *duration < 1)
        {
            notes.push_back("animation " + name + ": duration");
        }
        else if (steps->count > MAX_STEPS)
        {
            notes.push_back("animation " + name + ": too many steps");
        }
        else if (std::any_of(found->second.begin(), found->second.end(),
                             [](const KeyframeStop &stop) {
                                 return stop.percent > 0 && stop.percent < 100;
                             }))
        {
            notes.push_back("animation " + name + ": keyframes in between");
        }
        else
        {
            bool hasFrom = false;
            bool hasTo = false;
            const auto from = keyframePositionX(found->second, 0, props, *width,
                                                sheetW, hasFrom);
            const auto to = keyframePositionX(found->second, 100, props, *width,
                                              sheetW, hasTo);

            // a missing stop is the element's own value
            const double a = hasFrom ? from.value_or(x0) : x0;
            const double b = hasTo ? to.value_or(x0) : x0;
            if ((hasFrom && !from) || (hasTo && !to))
            {
                notes.push_back("animation " + name + ": keyframe position");
            }
            else if (a == b)
            {
                notes.push_back("animation " + name + ": doesn't move");
            }
            else
            {
                const int n = steps->count;
                const double step = (b - a) / n;
                for (int i = 0; i < n; i++)
                {
                    xs.push_back(a + step * (steps->jumpStart ? i + 1 : i));
                }

                const auto *direction = get("animation-direction");
                endX = b;
                if (direction != nullptr && *direction == "reverse")
                {
                    std::reverse(xs.begin(), xs.end());
                    endX = a;
                }
                else if (direction != nullptr && *direction != "normal")
                {
                    notes.push_back("animation-direction " + *direction);
                }

                // spread the milliseconds over the frames without losing any
                const auto total = static_cast<int>(std::lround(*duration));
                const int base = total / n;
                const int extra = total % n;
                for (int i = 0; i < n; i++)
                {
                    durations.push_back(base + (i < extra ? 1 : 0));
                }
            }
        }
    }

    if (xs.empty())
    {
        xs.push_back(x0);
        durations.push_back(0);
    }

    style.valid = true;
    style.frameSize = QSize(static_cast<int>(std::lround(*width)),
                            static_cast<int>(std::lround(*height)));
    style.sprite.frameSize = style.frameSize;
    for (const double x : xs)
    {
        // a background moved left shows a part of the image further right
        style.sprite.offsets.emplace_back(static_cast<int>(std::lround(-x)),
                                          static_cast<int>(std::lround(-y0)));
    }
    style.sprite.durationsMs = std::move(durations);

    // How often it plays, and what it shows afterwards. The CSS default is
    // one play; `infinite` is the only way to loop.
    if (style.sprite.offsets.size() > 1)
    {
        const auto first = [&](const char *name) -> std::optional<string> {
            const auto *raw = get(name);
            if (raw == nullptr)
            {
                return std::nullopt;
            }
            const auto resolved = resolveVars(*raw, props);
            if (!resolved)
            {
                return std::nullopt;
            }
            const auto parts = splitTopLevel(*resolved, ',');
            if (parts.empty())
            {
                return std::nullopt;
            }
            return toLower(trimView(parts.front()));
        };

        int loops = 1;
        if (const auto count = first("animation-iteration-count"))
        {
            if (*count == "infinite")
            {
                loops = 0;
            }
            else if (const auto n = parseNumber(*count); n && *n > 0)
            {
                // a part of a play (2.5) is rounded up
                loops = static_cast<int>(std::min(std::ceil(*n), 1e6));
            }
        }

        if (loops > 0)
        {
            const auto fill = first("animation-fill-mode");
            const bool keepEnd =
                fill && (*fill == "forwards" || *fill == "both");
            const double restX = keepEnd ? endX : x0;

            style.sprite.loopCount = loops;
            style.sprite.restOffset =
                QPoint(static_cast<int>(std::lround(-restX)),
                       static_cast<int>(std::lround(-y0)));
        }
        if (const auto delay = first("animation-delay");
            delay && *delay != "0s" && *delay != "0ms" && *delay != "0")
        {
            notes.emplace_back("animation-delay " + *delay);
        }
    }

    if (const auto *filter = get("filter"))
    {
        parseFilters(*filter, props, style.sprite.filters, notes);
    }

    // `background-repeat` is `repeat` unless the stylesheet says otherwise
    // (chat-gui's `.emote` doesn't), so a frame that starts or ends beyond the
    // sheet shows the sheet's other end instead of nothing.
    {
        bool tileX = true;
        bool tileY = true;
        std::vector<string> tokens;
        if (const auto *raw = get("background-repeat"))
        {
            if (const auto resolved = resolveVars(*raw, props))
            {
                tokens = splitWhitespace(toLower(*resolved));
            }
        }
        else if (const auto *shorthand = get("background"))
        {
            if (const auto resolved = resolveVars(*shorthand, props))
            {
                for (const auto &token : splitWhitespace(toLower(*resolved)))
                {
                    if (token == "no-repeat" || token == "repeat" ||
                        token == "repeat-x" || token == "repeat-y" ||
                        token == "space" || token == "round")
                    {
                        tokens.push_back(token);
                    }
                }
            }
        }
        const auto tiles = [](const string &token, bool horizontal) {
            if (token == "repeat")
            {
                return true;
            }
            if (token == "repeat-x")
            {
                return horizontal;
            }
            if (token == "repeat-y")
            {
                return !horizontal;
            }
            return false;  // no-repeat, space, round
        };
        if (tokens.size() == 1)
        {
            tileX = tiles(tokens[0], true);
            tileY = tiles(tokens[0], false);
        }
        else if (tokens.size() >= 2)
        {
            // two values: horizontal, then vertical
            tileX = tokens[0] == "repeat";
            tileY = tokens[1] == "repeat";
        }
        style.sprite.tileX = tileX;
        style.sprite.tileY = tileY;
    }

    bakeAnimations(props, keyframes, style, sheetSize, notes);
    applyStaticTransform(props, style, notes);

    // A `transform` is only reproduced where an animation moves it or, for a
    // sprite animation, where it stays the same (without its translation)
    std::vector<const char *> ignoredProps{"mask-image", "mix-blend-mode"};
    if (!style.sprite.hasPoses())
    {
        ignoredProps.push_back("transform");
    }
    for (const char *ignored : ignoredProps)
    {
        if (const auto *value = get(ignored);
            value != nullptr && *value != "none")
        {
            notes.emplace_back(ignored);
        }
    }

    for (const auto &note : notes)
    {
        style.notes.append(QString::fromStdString(note));
    }
    return style;
}

// ---------------------------------------------------------------------------
// `::before` and `::after`
// ---------------------------------------------------------------------------

std::optional<double> lengthPx(const anim::LenOrAuto &value, double reference)
{
    if (value.isAuto)
    {
        return std::nullopt;
    }
    return value.len.percent ? value.len.value * reference / 100.0
                             : value.len.value;
}

/// What a pseudo-element looks like when nothing animates.
anim::Computed pseudoBase(const Props &props)
{
    anim::Computed base;
    for (const char *name :
         {"left", "right", "top", "bottom", "width", "height", "font-size",
          "line-height", "letter-spacing", "content", "box-shadow", "opacity",
          "transform", "translate", "rotate", "scale", "transform-origin",
          "filter"})
    {
        const auto it = props.find(name);
        if (it == props.end())
        {
            continue;
        }
        auto resolved = resolveVars(it->second, props);
        if (!resolved)
        {
            continue;
        }
        if (auto parsed = anim::parseDeclaration(name, *resolved))
        {
            anim::setProp(base, parsed->first, std::move(parsed->second));
        }
    }
    return base;
}

/// The animations of `animation` (the whole list) that can be built.
std::vector<anim::Animation> pseudoAnimations(const Props &props,
                                              const Keyframes &keyframes,
                                              std::vector<string> &notes)
{
    std::vector<anim::Animation> out;
    const auto rawIt = props.find("animation-raw");
    if (rawIt == props.end())
    {
        return out;
    }
    const auto rawList = resolveVars(rawIt->second, props);
    if (!rawList || trimView(*rawList) == "none")
    {
        return out;
    }
    const auto specs = parseAnimationsOf(props, *rawList);
    if (!specs)
    {
        notes.push_back("pseudo-element animation (not understood): " +
                        *rawList);
        return out;
    }
    for (const auto &spec : *specs)
    {
        if (spec.name.empty() || spec.name == "none")
        {
            continue;
        }
        const auto found = keyframes.find(spec.name);
        if (found == keyframes.end())
        {
            notes.push_back("pseudo-element animation " + spec.name +
                            ": no @keyframes");
            continue;
        }
        std::vector<anim::Keyframe> frames;
        for (const auto &stop : found->second)
        {
            anim::Keyframe frame;
            frame.offset = stop.percent / 100.0;
            for (const auto &decl : stop.decls)
            {
                const auto value = resolveVars(decl.value, props);
                frame.decls.push_back(
                    anim::KeyframeDecl{decl.name, value ? *value : decl.value});
            }
            frames.push_back(std::move(frame));
        }
        string problem;
        auto built = anim::Animation::build(spec, frames, problem);
        if (!built)
        {
            notes.push_back("pseudo-element animation " + spec.name + ": " +
                            problem);
            continue;
        }
        out.push_back(std::move(*built));
    }
    return out;
}

/// What chat-gui does to the box of an emote while keyframes that never
/// change set its `width`, `margin-*` and `background-position`: NoTears
/// makes its box 69px wide to have room for the banana that flies around
/// next to it, and moves the picture inside so that it stays where it was.
struct BoxAnimation {
    /// 0 if not set
    double width = 0;
    /// `background-position` (x)
    double imageX = 0;
    /// Every animation that is only about the box (or `z-index`), so that
    /// the notes about them can go.
    std::vector<string> names;
    bool any = false;
};

BoxAnimation boxAnimationOf(const Props &emote, const Keyframes &keyframes)
{
    BoxAnimation out;
    const auto rawIt = emote.find("animation-raw");
    if (rawIt == emote.end())
    {
        return out;
    }
    const auto rawList = resolveVars(rawIt->second, emote);
    if (!rawList)
    {
        return out;
    }
    const auto specs = parseAnimationsOf(emote, *rawList);
    if (!specs)
    {
        return out;
    }

    for (const auto &spec : *specs)
    {
        const auto found = keyframes.find(spec.name);
        if (found == keyframes.end() || found->second.empty())
        {
            continue;
        }

        std::map<string, string> values;
        bool constant = true;
        bool onlyBox = true;
        for (const auto &stop : found->second)
        {
            for (const auto &decl : stop.decls)
            {
                const string name = toLower(decl.name);
                if (name == "z-index" || name == "animation-timing-function")
                {
                    continue;
                }
                if (name != "width" && name != "margin-left" &&
                    name != "margin-right" && name != "background-position")
                {
                    onlyBox = false;
                    continue;
                }
                const auto value = resolveVars(decl.value, emote);
                const string text = value ? *value : decl.value;
                const auto [it, inserted] = values.emplace(name, text);
                if (!inserted && it->second != text)
                {
                    constant = false;
                }
            }
        }
        // a property that only some stops have moves from / to the value the
        // element has by itself
        for (const auto &stop : found->second)
        {
            for (const auto &[name, text] : values)
            {
                const bool has = std::any_of(
                    stop.decls.begin(), stop.decls.end(), [&](const Decl &d) {
                        return toLower(d.name) == name;
                    });
                if (!has)
                {
                    constant = false;
                }
            }
        }
        if (!onlyBox)
        {
            continue;
        }
        out.names.push_back(spec.name);
        if (values.empty() || !constant)
        {
            continue;
        }

        out.any = true;
        if (const auto w = values.find("width"); w != values.end())
        {
            if (const auto px = parseLengthPx(w->second))
            {
                out.width = *px;
            }
        }
        if (const auto bg = values.find("background-position");
            bg != values.end())
        {
            const auto tokens = splitWhitespace(bg->second);
            if (!tokens.empty())
            {
                if (const auto px = parseLengthPx(tokens[0]))
                {
                    out.imageX = *px;
                }
            }
        }
    }
    return out;
}

SpriteOverlay overlayOf(const Props &props, const anim::Computed &state,
                        const anim::Box &box)
{
    SpriteOverlay o;
    o.text = state.content.none ? QString()
                                : QString::fromStdString(state.content.text);
    o.fontPx = state.fontSize;
    if (const auto it = props.find("font-family"); it != props.end())
    {
        o.fontFamily = QString::fromStdString(it->second);
    }
    if (const auto it = props.find("color"); it != props.end())
    {
        if (const auto resolved = resolveVars(it->second, props))
        {
            if (const auto color = anim::parseColorValue(*resolved))
            {
                o.color = *color;
            }
        }
    }
    o.letterSpacingPx = state.letterSpacing;
    if (!state.lineHeight.isAuto)
    {
        o.lineHeightPx =
            state.lineHeight.len.percent
                ? state.lineHeight.len.value * state.fontSize / 100.0
                : state.lineHeight.len.value;
    }
    o.left = lengthPx(state.left, box.width);
    o.top = lengthPx(state.top, box.height);
    o.right = lengthPx(state.right, box.width);
    o.bottom = lengthPx(state.bottom, box.height);
    o.width = lengthPx(state.width, box.width);
    o.height = lengthPx(state.height, box.height);

    const auto m = anim::matrixWithoutOrigin(state, box);
    o.matrix = {m.a, m.b, m.c, m.d, m.e, m.f};
    o.originX = state.origin.x.value;
    o.originXPercent = state.origin.x.percent;
    o.originY = state.origin.y.value;
    o.originYPercent = state.origin.y.percent;
    o.opacity = state.opacity;
    for (const auto &fn : state.filter)
    {
        o.filters.push_back(toSpriteFilter(fn));
    }
    for (const auto &shadow : state.boxShadow)
    {
        o.shadows.push_back(SpriteShadow{
            .inset = shadow.inset,
            .x = shadow.x,
            .y = shadow.y,
            .blur = shadow.blur,
            .spread = shadow.spread,
            .rgba = shadow.rgba,
        });
    }
    return o;
}

/// A box that certainly shows nothing: transparent, empty, or parked above
/// the emote (NoTears' banana waits at `top: -34px`).
bool overlayHidden(const SpriteOverlay &o, const anim::Box &box)
{
    if (o.opacity <= 0)
    {
        return true;
    }
    if (o.text.isEmpty() && o.shadows.empty())
    {
        return true;
    }
    if (o.top && !o.bottom && o.shadows.empty())
    {
        const double height = o.height.value_or(
            o.lineHeightPx > 0 ? o.lineHeightPx : o.fontPx * 1.3);
        if (*o.top + height <= 0 || *o.top >= box.height)
        {
            return true;
        }
    }
    return false;
}

/// Draws the `::before` and `::after` of an emote (GSN's dolphins, NoTears'
/// banana, HACKERMAN's scan line and digits) on top of its frames, as
/// `SpriteSheetSpec::overlays`. Both are laid out in the emote's box, run on
/// one clock, and are cut off at its edge. Leaves `style` alone (adding a
/// note) if they can't be drawn.
///
/// Not combined with an emote that has a sprite or pose animation of its
/// own; a plain `scale(-1, 1)` is fine.
void applyPseudoElements(const Props &emote, const Props *beforeProps,
                         const Props *afterProps, const Keyframes &keyframes,
                         DggEmoteStyle &style)
{
    if (!style.valid)
    {
        return;
    }
    std::vector<string> notes;
    auto &sprite = style.sprite;

    const double frameW = style.frameSize.width();
    const double frameH = style.frameSize.height();

    const auto boxAnimation = boxAnimationOf(emote, keyframes);
    const bool widened = boxAnimation.any && boxAnimation.width > frameW + 0.5;
    const double boxW = widened ? boxAnimation.width : frameW;
    const anim::Box box{.width = boxW, .height = frameH};

    struct Layer {
        const Props *props;
        anim::Computed base;
    };
    std::vector<Layer> layers;
    std::vector<anim::LayerInput> inputs;
    for (const Props *props : {beforeProps, afterProps})
    {
        if (props == nullptr)
        {
            continue;
        }
        const auto position = props->find("position");
        const auto display = props->find("display");
        if (display != props->end() && trimView(display->second) == "none")
        {
            continue;
        }
        auto base = pseudoBase(*props);
        if (base.content.none)
        {
            continue;  // no content, no box
        }
        if (position == props->end() ||
            trimView(position->second) != "absolute")
        {
            notes.emplace_back("pseudo-element that isn't position: absolute");
            continue;
        }
        if (const auto blend = props->find("mix-blend-mode");
            blend != props->end() && trimView(blend->second) != "normal")
        {
            notes.emplace_back("pseudo-element mix-blend-mode");
        }
        auto animations = pseudoAnimations(*props, keyframes, notes);
        inputs.push_back(anim::LayerInput{std::move(animations), base});
        layers.push_back(Layer{props, std::move(base)});
    }

    // the emote's own animations that were only about the box aren't news
    for (const auto &name : boxAnimation.names)
    {
        style.notes.removeIf([&](const QString &note) {
            return note.startsWith(QStringLiteral("animation %1:")
                                       .arg(QString::fromStdString(name)));
        });
    }

    if (layers.empty())
    {
        for (const auto &note : notes)
        {
            style.notes.append(QString::fromStdString(note));
        }
        return;
    }

    // An emote that only animates its own pose (a filter flash, a shake) and
    // plays a limited number of times (gachiGASM, SNAP, SOTRIGGERED) is
    // played together with its pseudo-elements: the two timelines are merged.
    // One that steps through its sprite sheet, or loops for ever, isn't.
    const bool single = sprite.offsets.size() == 1;
    const bool sameOffsets = std::all_of(
        sprite.offsets.begin(), sprite.offsets.end(), [&](const QPoint &p) {
            return p == sprite.offsets.front();
        });
    const bool posesDiffer =
        sprite.hasPoses() &&
        !std::all_of(sprite.poses.begin(), sprite.poses.end(),
                     [&](const SpritePose &pose) {
                         return pose == sprite.poses.front();
                     });
    if (!single && !(sameOffsets && posesDiffer && sprite.loopCount > 0))
    {
        style.notes.append(QStringLiteral(
            "pseudo-elements not drawn: the emote is animated itself"));
        return;
    }
    const bool mergeOwn = !single;

    const auto overlaysOf = [&](const std::vector<anim::Computed> &states) {
        std::vector<SpriteOverlay> list;
        for (size_t i = 0; i < states.size(); i++)
        {
            if (states[i].content.none)
            {
                continue;
            }
            auto overlay = overlayOf(*layers[i].props, states[i], box);
            if (!overlayHidden(overlay, box))
            {
                list.push_back(std::move(overlay));
            }
        }
        return list;
    };

    const QPoint offset = sprite.offsets.front();
    const SpritePose pose =
        sprite.hasPoses() ? sprite.poses.front() : SpritePose{};

    string problem;
    const auto baked =
        anim::bakeLayers(inputs, box, anim::BakeOptions{}, problem);
    if (baked && mergeOwn)
    {
        // the emote's own poses, `loopCount` times over
        constexpr int MAX_PLAYS = 50;
        constexpr int MIN_MS = 20;
        constexpr size_t MAX_SEGMENTS = 600;
        const auto &ownPoses = sprite.poses;
        const auto &ownMs = sprite.durationsMs;
        std::vector<int> ownStart;  // start of each frame within one play
        int cycle = 0;
        for (const int ms : ownMs)
        {
            ownStart.push_back(cycle);
            cycle += std::max(MIN_MS, ms);
        }
        const int plays = std::min(sprite.loopCount, MAX_PLAYS);
        const int ownTotal = cycle * plays;

        std::vector<int> overlayStart;
        int overlayTotal = 0;
        for (const auto &frame : baked->frames)
        {
            overlayStart.push_back(overlayTotal);
            overlayTotal += std::max(MIN_MS, frame.durationMs);
        }

        std::vector<int> bounds{0};
        {
            std::vector<int> all;
            for (int play = 0; play < plays; play++)
            {
                for (const int start : ownStart)
                {
                    all.push_back(play * cycle + start);
                }
            }
            for (const int start : overlayStart)
            {
                all.push_back(start);
            }
            all.push_back(ownTotal);
            all.push_back(overlayTotal);
            std::sort(all.begin(), all.end());
            for (const int t : all)
            {
                // two moments closer than a frame can last are one
                if (t - bounds.back() >= MIN_MS)
                {
                    bounds.push_back(t);
                }
            }
        }
        const int total = std::max(ownTotal, overlayTotal);
        if (bounds.size() < 2 || bounds.size() > MAX_SEGMENTS || total <= 0)
        {
            style.notes.append(QStringLiteral(
                "pseudo-elements not drawn: too many frames to combine with "
                "the emote's own animation"));
            return;
        }
        if (bounds.back() < total)
        {
            bounds.push_back(total);
        }

        const auto indexAt = [](const std::vector<int> &starts, int t) {
            size_t k = 0;
            while (k + 1 < starts.size() && starts[k + 1] <= t)
            {
                k++;
            }
            return k;
        };

        std::vector<SpritePose> poses;
        std::vector<std::vector<SpriteOverlay>> overlays;
        std::vector<int> durations;
        for (size_t i = 0; i + 1 < bounds.size(); i++)
        {
            const int t = bounds[i];
            poses.push_back(t >= ownTotal
                                ? sprite.restPose
                                : ownPoses[indexAt(ownStart, t % cycle)]);
            overlays.push_back(
                t >= overlayTotal
                    ? overlaysOf(baked->rest)
                    : overlaysOf(
                          baked->frames[indexAt(overlayStart, t)].layers));
            durations.push_back(bounds[i + 1] - t);
        }
        if (poses.size() == 1)
        {
            poses.push_back(poses.front());
            overlays.push_back(overlays.front());
            durations.push_back(durations.front());
        }
        const auto rest = sprite.restPose;
        sprite.offsets.assign(poses.size(), offset);
        sprite.durationsMs = std::move(durations);
        sprite.poses = std::move(poses);
        sprite.overlays = std::move(overlays);
        sprite.restOverlays = overlaysOf(baked->rest);
        sprite.restPose = rest;
        sprite.loopCount = 1;
        sprite.restOffset = offset;
        if (baked->truncated)
        {
            notes.emplace_back("pseudo-element animation: cut off after 10 s");
        }
    }
    else if (baked)
    {
        auto frames = baked->frames;
        if (frames.size() == 1)
        {
            // what makes a limited animation one
            frames.push_back(frames.front());
        }
        sprite.offsets.assign(frames.size(), offset);
        sprite.durationsMs.clear();
        sprite.overlays.clear();
        for (const auto &frame : frames)
        {
            sprite.durationsMs.push_back(frame.durationMs);
            sprite.overlays.push_back(overlaysOf(frame.layers));
        }
        sprite.restOverlays = overlaysOf(baked->rest);
        sprite.loopCount = 1;
        sprite.restOffset = offset;
        if (sprite.hasPoses())
        {
            sprite.poses.assign(frames.size(), pose);
            sprite.restPose = pose;
        }
        if (baked->truncated)
        {
            notes.emplace_back("pseudo-element animation: cut off after 10 s");
        }
    }
    else if (problem == "no duration" || problem == "nothing changes")
    {
        // nothing moves: one picture
        std::vector<anim::Computed> states;
        for (const auto &input : inputs)
        {
            states.push_back(
                anim::computeAt(input.animations, input.base, 0, box));
        }
        if (mergeOwn)
        {
            sprite.overlays.assign(sprite.offsets.size(), overlaysOf(states));
            sprite.restOverlays = overlaysOf(states);
        }
        else
        {
            sprite.overlays.assign(1, overlaysOf(states));
        }
    }
    else
    {
        notes.push_back("pseudo-element animation: " + problem);
    }

    if (sprite.hasOverlays())
    {
        // `transform: scale(-1, 1)` on the emote flips its pseudo-elements
        // and the box they live in as well
        bool mirrored = false;
        if (const auto it = emote.find("transform"); it != emote.end())
        {
            if (const auto resolved = resolveVars(it->second, emote))
            {
                if (auto parsed = anim::parseDeclaration(
                        "transform", withoutTranslations(*resolved)))
                {
                    anim::Computed c;
                    anim::setProp(c, parsed->first, std::move(parsed->second));
                    const auto m = anim::matrixWithoutOrigin(
                        c, anim::Box{.width = frameW, .height = frameH});
                    mirrored = m.a < 0 && m.d > 0;
                }
            }
        }
        sprite.canvasMirror = mirrored;
        if (widened)
        {
            const int canvasW = static_cast<int>(std::lround(boxW));
            const int imageX =
                static_cast<int>(std::lround(boxAnimation.imageX));
            sprite.canvasSize = QSize(canvasW, style.frameSize.height());
            sprite.canvasImagePos = QPoint(
                mirrored ? canvasW - imageX - style.frameSize.width() : imageX,
                0);
        }
    }

    for (const auto &note : notes)
    {
        style.notes.append(QString::fromStdString(note));
    }
}

/// `background-size` as pixels. A single value is the width; the height is
/// `auto`, i.e. keeps the proportions of the image.
std::optional<QSizeF> parseBackgroundSizeAuto(string_view raw,
                                              const Props &props,
                                              QSize sheetSize)
{
    const auto resolved = resolveVars(raw, props);
    if (!resolved || sheetSize.isEmpty())
    {
        return std::nullopt;
    }
    const string_view text = trimView(*resolved);
    const auto space = text.find(' ');
    const auto first = parseLengthPx(text.substr(0, space));
    if (!first || *first < 1)
    {
        return std::nullopt;
    }
    double height = *first * sheetSize.height() / sheetSize.width();
    if (space != string_view::npos)
    {
        const auto rest = trimView(text.substr(space + 1));
        if (rest != "auto")
        {
            const auto second = parseLengthPx(rest);
            if (!second || *second < 1)
            {
                return std::nullopt;
            }
            height = *second;
        }
    }
    return QSizeF(*first, height);
}

/// An emote of a combo line whose box is made of several copies of its image
/// (ARRIVE): the emote itself is wider than the image and repeats it, and its
/// `::before` / `::after` are blocks in its flow that inherit its background
/// and draw more copies, brighter, on top. `.emote` clips all of it to its
/// box. Fills `style.sprite.imageLayers` (and the mask) and returns true if it
/// applies: some pseudo-element is a static box with content, or the emote has
/// a `mask-image`. Pseudo-elements with `position: absolute` are left to
/// applyPseudoElements().
bool applyImageLayers(const Props &emote, const Props *beforeProps,
                      const Props *afterProps, QSize sheetSize,
                      DggEmoteStyle &style)
{
    if (!style.valid || sheetSize.isEmpty())
    {
        return false;
    }
    const auto text = [](const Props &p, const char *name) -> const string * {
        const auto it = p.find(name);
        return it == p.end() ? nullptr : &it->second;
    };
    const auto isStaticBox = [&](const Props *p) {
        if (p == nullptr)
        {
            return false;
        }
        const auto *content = text(*p, "content");
        const auto *display = text(*p, "display");
        const auto *position = text(*p, "position");
        return content != nullptr && trimView(*content) != "none" &&
               (display == nullptr || trimView(*display) != "none") &&
               (position == nullptr || trimView(*position) != "absolute");
    };
    const bool before = isStaticBox(beforeProps);
    const bool after = isStaticBox(afterProps);
    const auto *maskRaw = text(emote, "mask-image");
    std::optional<SpriteRadialMask> mask;
    if (maskRaw != nullptr)
    {
        // radial-gradient(black 65%, transparent 80%)
        static const std::regex re(
            R"(radial-gradient\(\s*black\s+([0-9.]+)%\s*,\s*transparent\s+([0-9.]+)%\s*\))");
        std::smatch m;
        const string value = toLower(*maskRaw);
        if (std::regex_search(value, m, re))
        {
            // `[0-9.]+` also matches "." and "1.2.3"
            const auto inner = parseNumber(m[1].str());
            const auto outer = parseNumber(m[2].str());
            if (inner && outer)
            {
                mask = SpriteRadialMask{*inner, *outer};
            }
        }
    }
    if (!before && !after && !mask)
    {
        return false;
    }

    std::vector<string> notes;
    const double boxW = style.frameSize.width();
    const double boxH = style.frameSize.height();

    // `background-repeat` of one element: its own tokens, else (for a
    // pseudo-element with `background: inherit`) the emote's, else `repeat`
    const auto repeatOf = [&](const Props &p, bool inherit, bool &tileX,
                              bool &tileY) {
        const Props *from = &p;
        if (text(p, "background-repeat") == nullptr)
        {
            if (!inherit)
            {
                tileX = tileY = true;
                return;
            }
            from = &emote;
        }
        tileX = tileY = true;
        const auto *raw = text(*from, "background-repeat");
        if (raw == nullptr)
        {
            return;
        }
        const auto tokens = splitWhitespace(toLower(*raw));
        if (tokens.size() == 1)
        {
            tileX = tokens[0] == "repeat" || tokens[0] == "repeat-x";
            tileY = tokens[0] == "repeat" || tokens[0] == "repeat-y";
        }
        else if (tokens.size() >= 2)
        {
            tileX = tokens[0] == "repeat";
            tileY = tokens[1] == "repeat";
        }
    };

    const auto makeLayer = [&](const Props &p, bool inherit,
                               const QRectF &box) -> SpriteImageLayer {
        SpriteImageLayer layer;
        layer.box = box;
        const auto pick = [&](const char *name) -> const string * {
            if (const auto *v = text(p, name))
            {
                return v;
            }
            return inherit ? text(emote, name) : nullptr;
        };
        if (const auto *size = pick("background-size"))
        {
            if (const auto parsed =
                    parseBackgroundSizeAuto(*size, p, sheetSize))
            {
                layer.imageSize = *parsed;
            }
        }
        const QSizeF drawn =
            layer.imageSize.isEmpty() ? QSizeF(sheetSize) : layer.imageSize;
        if (const auto *x = pick("background-position-x"))
        {
            if (const auto v = positionPx(*x, p, box.width(), drawn.width()))
            {
                layer.imagePos.setX(*v);
            }
        }
        if (const auto *y = pick("background-position-y"))
        {
            if (const auto v = positionPx(*y, p, box.height(), drawn.height()))
            {
                layer.imagePos.setY(*v);
            }
        }
        repeatOf(p, inherit, layer.tileX, layer.tileY);
        if (const auto *filter = text(p, "filter"))
        {
            parseFilters(*filter, p, layer.filters, notes);
        }
        if (const auto *opacity = text(p, "opacity"))
        {
            if (const auto v = parseNumber(trimView(*opacity)))
            {
                layer.opacity = *v;
            }
        }
        return layer;
    };

    std::vector<SpriteImageLayer> layers;
    layers.push_back(makeLayer(emote, false, QRectF(0, 0, boxW, boxH)));

    // the pseudo-elements are blocks one below the other in the emote
    const auto lengthOf = [&](const Props &p, const char *name) {
        const auto *raw = text(p, name);
        if (raw == nullptr)
        {
            return 0.0;
        }
        const auto resolved = resolveVars(*raw, p);
        return resolved ? parseLengthPx(*resolved).value_or(0.0) : 0.0;
    };
    double flowY = 0;
    double previousBottomMargin = 0;
    bool first = true;
    for (const Props *p :
         {before ? beforeProps : nullptr, after ? afterProps : nullptr})
    {
        if (p == nullptr)
        {
            continue;
        }
        const double marginTop = lengthOf(*p, "margin-top");
        const double marginLeft = lengthOf(*p, "margin-left");
        const double w = lengthOf(*p, "width");
        const double h = lengthOf(*p, "height");
        if (w < 1 || h < 1)
        {
            notes.emplace_back("pseudo-element without a size");
            continue;
        }
        // the margins of neighbouring blocks collapse; the first one is
        // inside a box that clips, so its margin stays
        double gap = marginTop;
        if (!first)
        {
            const double a = previousBottomMargin;
            const double b = marginTop;
            gap = (a >= 0 && b >= 0) ? std::max(a, b)
                  : (a < 0 && b < 0) ? std::min(a, b)
                                     : a + b;
        }
        const double top = flowY + gap;
        const auto *bg = text(*p, "background");
        const bool inherit = bg != nullptr && trimView(*bg) == "inherit";
        layers.push_back(makeLayer(*p, inherit, QRectF(marginLeft, top, w, h)));
        flowY = top + h;
        previousBottomMargin = lengthOf(*p, "margin-bottom");
        first = false;
    }

    style.sprite.imageLayers = std::move(layers);
    style.sprite.mask = mask;
    style.sprite.filters.clear();
    style.sprite.offsets.assign(1, QPoint(0, 0));
    style.sprite.durationsMs.assign(1, 0);
    style.backgroundSize = QSize();
    style.notes.removeIf([](const QString &note) {
        return note == QStringLiteral("mask-image");
    });
    for (const auto &note : notes)
    {
        style.notes.append(QString::fromStdString(note));
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// DggEmoteStyle
// ---------------------------------------------------------------------------

bool DggEmoteStyle::isWholeImage(QSize sheetSize) const
{
    return this->valid && !sheetSize.isEmpty() && !this->sprite.hasPoses() &&
           this->sprite.offsets.size() == 1 &&
           this->sprite.offsets[0] == QPoint(0, 0) &&
           this->frameSize == sheetSize && this->sprite.filters.empty() &&
           !this->sprite.hasImageLayers();
}

QString DggEmoteStyle::cacheKey() const
{
    if (!this->valid)
    {
        return QStringLiteral("plain");
    }
    return this->sprite.cacheKey() + QStringLiteral("|bg%1x%2")
                                         .arg(this->backgroundSize.width())
                                         .arg(this->backgroundSize.height());
}

// ---------------------------------------------------------------------------
// DggEmoteCss
// ---------------------------------------------------------------------------

DggEmoteCss DggEmoteCss::parse(const QString &css)
{
    DggEmoteCss out;
    const string text = stripComments(css.toStdString());
    int order = 0;

    forEachBlock(text, [&](string_view prelude, string_view body) {
        if (startsWith(prelude, "@keyframes") ||
            startsWith(prelude, "@-webkit-keyframes"))
        {
            const auto fields = splitWhitespace(prelude);
            if (fields.size() < 2)
            {
                return;
            }

            std::vector<KeyframeStop> stops;
            forEachBlock(body, [&](string_view selector, string_view stopBody) {
                const auto decls = parseDeclarations(stopBody);
                for (const auto &part : splitTopLevel(selector, ','))
                {
                    const auto key = toLower(trimView(part));
                    double percent = 0;
                    if (key == "from")
                    {
                        percent = 0;
                    }
                    else if (key == "to")
                    {
                        percent = 100;
                    }
                    else if (!key.empty() && key.back() == '%')
                    {
                        const auto number = parseNumber(
                            string_view(key).substr(0, key.size() - 1));
                        if (!number)
                        {
                            continue;
                        }
                        percent = *number;
                    }
                    else
                    {
                        continue;
                    }
                    stops.push_back(KeyframeStop{percent, decls});
                }
            });
            out.keyframes_[fields[1]] = std::move(stops);
            return;
        }

        if (startsWith(prelude, "@"))
        {
            // @supports, @media, ... aren't looked into
            out.unsupported_["at-rule"]++;
            out.stats_.unsupportedRules++;
            return;
        }

        const auto decls = parseDeclarations(body);
        for (const auto &selector : splitTopLevel(prelude, ','))
        {
            auto analysis = analyseSelector(trimView(selector));
            if (!analysis.rejected.empty())
            {
                out.unsupported_[analysis.rejected]++;
                out.stats_.unsupportedRules++;
                continue;
            }
            if (analysis.hover)
            {
                out.stats_.hoverRules++;
                continue;
            }

            Rule rule;
            rule.parts = std::move(analysis.parts);
            rule.specificity = analysis.specificity;
            rule.order = ++order;
            rule.dependsOnContext = analysis.dependsOnContext;
            rule.pseudo = analysis.pseudo;
            rule.decls = decls;
            out.rules_[analysis.emote].push_back(std::move(rule));
            out.stats_.rules++;
        }
    });

    out.stats_.keyframes = out.keyframes_.size();
    out.stats_.emotes = out.rules_.size();
    return out;
}

QStringList DggEmoteCss::describeUnsupported() const
{
    std::vector<std::pair<std::string, size_t>> sorted(
        this->unsupported_.begin(), this->unsupported_.end());
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto &a, const auto &b) {
                         return a.second > b.second;
                     });

    QStringList out;
    for (const auto &[reason, count] : sorted)
    {
        out.append(QString::fromStdString(reason) + QStringLiteral(": ") +
                   QString::number(static_cast<qulonglong>(count)));
    }
    return out;
}

bool DggEmoteCss::hasEmote(const QString &emote) const
{
    return this->rules_.contains(emote.toStdString());
}

bool DggEmoteCss::dependsOnContext(const QString &emote) const
{
    const auto it = this->rules_.find(emote.toStdString());
    if (it == this->rules_.end())
    {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(),
                       [](const Rule &rule) {
                           return rule.dependsOnContext;
                       });
}

DggEmoteStyle DggEmoteCss::resolve(const QString &emote,
                                   const DggEmoteContext &context,
                                   QSize sheetSize) const
{
    const string name = emote.toStdString();
    const auto it = this->rules_.find(name);
    if (it == this->rules_.end())
    {
        return {};
    }

    const Dom dom = buildDom(name, context);
    std::vector<const Rule *> matched;
    for (const auto &rule : it->second)
    {
        if (matchChain(rule.parts, rule.parts.size() - 1, dom, dom.emote))
        {
            matched.push_back(&rule);
        }
    }
    if (matched.empty())
    {
        return {};
    }

    // The cascade: higher specificity wins, later rules win ties, and
    // `!important` beats both.
    std::stable_sort(matched.begin(), matched.end(),
                     [](const Rule *a, const Rule *b) {
                         if (a->specificity != b->specificity)
                         {
                             return a->specificity < b->specificity;
                         }
                         return a->order < b->order;
                     });

    Props props;
    for (const bool important : {false, true})
    {
        for (const auto *rule : matched)
        {
            if (rule->pseudo != 0)
            {
                continue;
            }
            for (const auto &decl : rule->decls)
            {
                if (decl.important == important)
                {
                    setProperty(props, decl.name, decl.value);
                }
            }
        }
    }

    // `::before` and `::after`: their own properties, but custom properties
    // are inherited (NoTears' `--t-NoTears` is set on the emote and used by
    // its `::before`)
    std::array<Props, 2> pseudoProps;
    std::array<bool, 2> hasPseudo{false, false};
    for (const bool important : {false, true})
    {
        for (const auto *rule : matched)
        {
            if (rule->pseudo == 0)
            {
                continue;
            }
            const auto index = static_cast<size_t>(rule->pseudo - 1);
            if (!hasPseudo[index])
            {
                hasPseudo[index] = true;
                for (const auto &[key, value] : props)
                {
                    if (key.rfind("--", 0) == 0)
                    {
                        pseudoProps[index][key] = value;
                    }
                }
            }
            for (const auto &decl : rule->decls)
            {
                if (decl.important == important)
                {
                    setProperty(pseudoProps[index], decl.name, decl.value);
                }
            }
        }
    }

    auto style = computeStyle(props, this->keyframes_, sheetSize);
    // combo lines whose box is made of several copies of the image
    const bool layered =
        context.comboMessage && style.valid &&
        applyImageLayers(props, hasPseudo[0] ? &pseudoProps[0] : nullptr,
                         hasPseudo[1] ? &pseudoProps[1] : nullptr, sheetSize,
                         style);
    if (!layered && style.valid && (hasPseudo[0] || hasPseudo[1]))
    {
        applyPseudoElements(props, hasPseudo[0] ? &pseudoProps[0] : nullptr,
                            hasPseudo[1] ? &pseudoProps[1] : nullptr,
                            this->keyframes_, style);
    }
    if (style.valid && style.sprite.canvasSize.isEmpty())
    {
        // room around the frame for the glow of a drop-shadow()
        int reach = dropShadowReach(style.sprite.filters);
        for (const auto &pose : style.sprite.poses)
        {
            reach = std::max(reach, dropShadowReach(pose.filters));
        }
        reach = std::max(reach, dropShadowReach(style.sprite.restPose.filters));
        style.sprite.glowMargin = std::min(reach, MAX_GLOW_MARGIN);
    }
    return style;
}

}  // namespace chatterino
