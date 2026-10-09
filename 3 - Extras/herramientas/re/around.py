from q import *
def around(a, before=0x60, after=0x40):
    f=func_of(IB+a); o=dis(f, 5000).split('\n')
    for l in o:
        try: x=int(l.split(':')[0],16)
        except: continue
        if a-before<=x<=a+after: print(l)
    print('=====', hex(f-IB))
