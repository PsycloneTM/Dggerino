# NoTears / selector check (standalone, needs only g++ and python3 with soupsieve + bs4)

Compiles the real selector parser, `analyseSelector()` and matcher of `DggEmoteCss.cpp` (no Qt) and compares
them with a real CSS engine (soupsieve) on the site's own NoTears selectors.

    python3 make_harness.py ../..            # run from tools/notears-check, repo root is ../..
    g++ -std=c++20 -O1 -w -I. driver.cpp -o driver
    python3 compare.py                       # expect: 0 rejected selectors, 0 mismatches

`notears_fixture.css` is the exact NoTears block of the site's emotes.css. Run `./driver full.css` on the
whole emotes.css (CR stripped) to list every selector the analysis rejects.
