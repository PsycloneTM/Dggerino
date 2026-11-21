import itertools, subprocess
from notears_truth import classify
toks="NLUKCRS"
seqs=[''.join(p) for n in range(1,6) for p in itertools.product(toks,repeat=n) if 'N' in p]
out=subprocess.run(['./driver','notears_fixture.css'],input='\n'.join(seqs)+'\n',capture_output=True,text=True).stdout.splitlines()
print(out[0]); rej=[l for l in out if l.startswith('REJECTED')]; print('rejected selectors:',len(rej)); [print(' ',r) for r in rej]
res=[l for l in out if ' -> ' in l]
bad=0
for s,l in zip(seqs,res):
    got=[w for w in l.split(' -> ')[1].split()]
    want=classify(s)
    if got!=want:
        bad+=1
        if bad<=10: print('MISMATCH',s,'got',got,'want',want)
print(f'{len(seqs)} sequences compared, {bad} mismatches')
