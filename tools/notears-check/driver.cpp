#include "harness_body.inc"
#include <fstream>
#include <sstream>
using namespace chatterino;
using namespace chatterino::dggcss;
namespace {
struct R { Rule rule; };
}
int main(int argc, char **argv)
{
    std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf();
    const std::string text = stripComments(ss.str());
    std::vector<Rule> rules; int order = 0; std::map<std::string,int> rejected;
    forEachBlock(text, [&](string_view prelude, string_view body) {
        if (startsWith(prelude, "@")) return;
        const auto decls = parseDeclarations(body);
        for (const auto &sel : splitTopLevel(prelude, ',')) {
            auto a = analyseSelector(trimView(sel));
            if (!a.rejected.empty()) { rejected[a.rejected + "  <=  " + std::string(trimView(sel))]++; continue; }
            if (a.hover) continue;
            Rule r; r.parts = std::move(a.parts); r.specificity = a.specificity; r.order = ++order;
            r.pseudo = a.pseudo; r.decls = decls; rules.push_back(std::move(r));
        }
    });
    std::cout << "rules loaded: " << rules.size() << "\n";
    for (auto &[k,v] : rejected) std::cout << "REJECTED: " << k << "\n";

    // sequences like the python side: N=NoTears L=LUL U=chat-user K=link C=Clap R=RapThis S=Sippy
    const std::map<char,string> tok = {{'N',"NoTears"},{'L',"LUL"},{'C',"Clap"},{'R',"RapThis"},{'S',"Sippy"},{'U',CHAT_USER},{'K',""}};
    for (std::string seq; std::getline(std::cin, seq);) {
        std::cout << seq << " ->";
        for (int i = 0; i < (int)seq.size(); i++) {
            if (seq[i] != 'N') continue;
            Ctx ctx; for (char c : seq) ctx.elements.push_back(tok.at(c)); ctx.index = i;
            Dom dom = buildDomStd(ctx);
            std::vector<const Rule*> m;
            for (auto &r : rules) if (matchChain(r.parts, r.parts.size()-1, dom, dom.emote)) m.push_back(&r);
            std::stable_sort(m.begin(), m.end(), [](const Rule*a,const Rule*b){ return a->specificity!=b->specificity ? a->specificity<b->specificity : a->order<b->order; });
            std::map<string,string> self, before;
            for (bool imp : {false,true}) for (auto *r : m) for (auto &d : r->decls) if (d.important==imp) (r->pseudo==0?self:before)[d.name]=d.value;
            const string anim = self["animation"], banim = before["animation"];
            string kind = anim=="none" ? "still" : anim.find("flipped")!=string::npos ? "flipped+banana" : "wide+banana";
            if (kind=="still" && banim!="none") kind += "(!banana)";
            if (kind!="still" && banim=="none") kind += "(!no-banana)";
            std::cout << " " << kind;
        }
        std::cout << "\n";
    }
}
