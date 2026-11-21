"""Builds harness_body.inc from the REAL selector code in src/providers/dgg/DggEmoteCss.{hpp,cpp}
(pass the repository root, default ../..). Only buildDom() is re-expressed over std::string."""
import sys, re
root = sys.argv[1] if len(sys.argv) > 1 else '../..'
d = root + '/src/providers/dgg/'
cpp = open(d + 'DggEmoteCss.cpp').read().split('\n')
hpp = open(d + 'DggEmoteCss.hpp').read().split('\n')
def line(pat, text):
    return next(i for i, l in enumerate(text) if re.match(pat, l))
h0, h1 = line(r'namespace dggcss \{', hpp), line(r'\}  // namespace dggcss', hpp)
a = line(r'using std::string;', cpp); b = line(r'Dom buildDom\(', cpp)
c = line(r'bool isSubset\(', cpp); e = line(r'// Values', cpp)
head = '''#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
namespace chatterino {
'''
custom = '''struct Ctx { std::vector<string> elements; int index = 0; bool greentext = false; };
const string CHAT_USER = " chat-user";
Dom buildDomStd(const Ctx &ctx)
{
    Dom dom;
    Node m; m.kind = 'M'; m.tag = 'd'; m.classes = {"msg-chat", "msg-user"}; m.username = "someone";
    const int mIndex = addNode(dom, std::move(m), -1);
    Node u; u.kind = 'U'; u.tag = 'a'; u.classes = {"user"};
    addNode(dom, std::move(u), mIndex);
    Node t; t.kind = 'T'; t.tag = 's'; t.classes = {"text"};
    int container = addNode(dom, std::move(t), mIndex);
    if (ctx.greentext) { Node g; g.kind = 'G'; g.tag = 's'; g.classes = {"greentext"}; container = addNode(dom, std::move(g), container); }
    for (int i = 0; i < (int)ctx.elements.size(); i++) {
        Node e;
        if (ctx.elements[i] == CHAT_USER) { e.kind = 'L'; e.tag = 's'; e.classes = {"chat-user"}; }
        else if (ctx.elements[i].empty()) { e.kind = 'L'; e.tag = 'a'; e.classes = {"externallink"}; }
        else { e.kind = 'E'; e.tag = 'd'; e.classes = {"emote", ctx.elements[i]}; }
        const int node = addNode(dom, std::move(e), container);
        if (i == ctx.index) dom.emote = node;
    }
    return dom;
}
'''
out = (head + '\n'.join(hpp[h0:h1 + 1]) + '\nusing namespace dggcss;\nnamespace {\n' +
       '\n'.join(cpp[a:b]) + '\n' + custom + '\n'.join(cpp[c:e - 1]) +
       '\n}  // namespace\n}  // namespace chatterino\n')
open('harness_body.inc', 'w').write(out)
