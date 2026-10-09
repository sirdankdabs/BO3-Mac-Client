import socket, struct, time, random
def q(addr, payload, timeout=3, multi=False):
    s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(timeout)
    s.sendto(b'\xff\xff\xff\xff'+payload, addr); out=[]
    try:
        while True:
            d,_=s.recvfrom(65535); out.append(d)
            if not multi or b'EOT' in d: break
    except socket.timeout: pass
    return out
servers=[]
for host in ['master.ezz.lol','m.ezz.lol']:
    try: ip=socket.gethostbyname(host)
    except Exception as e: print(host,'dns fail',e); continue
    r=q((ip,20810), b'getservers\nT7 8 full empty', multi=True)
    print(host, ip, 'packets', len(r))
    for d in r:
        body=d[4:]; i=body.find(b'\\')
        while i!=-1 and i+7<=len(body):
            chunk=body[i+1:i+7]
            if chunk[:3]==b'EOT': break
            servers.append((socket.inet_ntoa(chunk[:4]), struct.unpack('>H',chunk[4:6])[0])); i+=7
            if i>=len(body) or body[i:i+1]!=b'\\': i=body.find(b'\\',i)
    if servers: break
servers=list(dict.fromkeys(servers)); print('servers', len(servers))
zm=0
for addr in servers[:60]:
    r=q(addr, b'getInfo\n'+str(random.randint(1,1<<30)).encode(), timeout=1)
    if not r: continue
    info=r[0][4:].split(b'\n',1)[1].decode('latin1').strip('\\').split('\\')
    d=dict(zip(info[0::2], info[1::2]))
    if d.get('playmode')=='0' or 'zm' in d.get('mapname',''):
        zm+=1
        if zm<=3: print(addr, {k:d[k] for k in sorted(d)})
print('zombies servers seen', zm)
