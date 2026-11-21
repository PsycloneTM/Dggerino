"""Ground truth for NoTears from the site's own selectors (emotes.css 6473-6531),
evaluated by a real CSS engine (soupsieve) on the DOM chat-gui builds:
.text > [ .emote | .chat-user | a.externallink ]*   (plain words are text nodes)."""
import itertools, soupsieve as sv
from bs4 import BeautifulSoup

STILL = """.emote.NoTears:only-child,
.emote + .emote.NoTears:last-child,
.chat-user + .emote.NoTears,
.emote.NoTears:has(+ .emote.Clap),
.emote.NoTears:has(+ .emote.RapThis),
.emote.NoTears:has(+ .emote.Sippy),
.emote.NoTears:has(+ .emote.ZoomerSippy),
.emote.NoTears:has(+ .emote.BoomerSippy),
.emote.NoTears:has(+ .emote.NoTears)"""
FLIPPED = """.emote.NoTears + .chat-user + .emote.NoTears,
.emote.NoTears + .emote:not(.NoTears) + .emote.NoTears"""

TOK = {
    "N": '<div class="emote NoTears"></div>',
    "L": '<div class="emote LUL"></div>',
    "C": '<div class="emote Clap"></div>',
    "R": '<div class="emote RapThis"></div>',
    "S": '<div class="emote Sippy"></div>',
    "U": '<span class="chat-user"></span>',
    "K": '<a class="externallink"></a>',
}

def classify(seq):
    html = '<span class="text">' + " ".join(TOK[c] for c in seq) + "</span>"
    soup = BeautifulSoup(html, "html.parser")
    text = soup.select_one(".text")
    still = {id(t) for t in sv.select(STILL, text)}
    flipped = {id(t) for t in sv.select(FLIPPED, text)}
    out = []
    for el, c in zip([e for e in text.children if getattr(e, "name", None)], seq):
        if c != "N":
            continue
        # flipped specificity (5-6 classes) beats still (<=4)
        out.append("flipped+banana" if id(el) in flipped
                   else "still" if id(el) in still
                   else "wide+banana")
    return out

if __name__ == "__main__":
    cases = ["N", "LN", "NN", "UN", "NL", "NU", "NK", "NLN", "NUN", "NKN", "NC",
             "NR", "NS", "NNN", "LNL", "NLNN", "NLLN", "NUUN", "UNL", "KN", "NLUN", "NNL"]
    for s in cases:
        print(f"{s:6s}", classify(s))
