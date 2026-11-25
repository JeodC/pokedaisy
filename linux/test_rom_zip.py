#!/usr/bin/env python3
"""Zipped ROMs (rom_zip.c): which entry is the ROM, its bytes, the unpacked copy, and zips that aren't what they say."""
import os,random,subprocess,sys,tempfile,zipfile,zlib
from pathlib import Path

binary=[*sys.argv[1:-1],str(Path(sys.argv[-1]).resolve())] # a cross build's emulator before it
def run(cmd,*args):
    r=subprocess.run([*binary,cmd,*map(str,args)],capture_output=True,text=True,timeout=60)
    assert r.returncode==0,(cmd,args,r.returncode,r.stderr)
    return r.stdout.rstrip('\n')
def crc(data):return f'{zlib.crc32(data):08x}'
def make(path,*entries,how=zipfile.ZIP_DEFLATED,comment=b''):
    with zipfile.ZipFile(path,'w',how) as z:
        for name,data in entries:z.writestr(name,data)
        z.comment=comment
    return path

rnd=random.Random(1)
rom=bytes(rnd.randrange(256) for _ in range(300000))+b'\0'*700000
other=bytes(rnd.randrange(256) for _ in range(2048))
with tempfile.TemporaryDirectory(prefix='pd-zip-') as d:
    d=Path(d);data=d/'data';data.mkdir()
    (d/'loose.gba').write_bytes(rom)
    assert run('name',d/'loose.gba')=='loose.gba' and run('crc',d/'loose.gba')==crc(rom)
    assert run('head',d/'loose.gba')==f'336 {crc(rom[:336])}'
    # Deflated and stored, with a comment after the directory.
    for how in (zipfile.ZIP_DEFLATED,zipfile.ZIP_STORED):
        z=make(d/'Game (USA).zip',('Game.gba',rom),how=how,comment=b'PK\5\6 a comment')
        assert run('name',z)=='Game (USA).gba',how
        assert run('crc',z)==crc(rom),how
        assert run('head',z)==f'336 {crc(rom[:336])}',how
    # The .gba, then a .gb / .gbc, then the biggest; folders dropped; macOS's files, text and tiny files skipped.
    assert run('name',make(d/'a.zip',('x.nds',rom),('y.gb',other),('in/z.GBA',other)))=='a.gba'
    assert run('name',make(d/'a.zip',('x.nds',rom),('y.gb',other),('y2.gbc',rom)))=='a.gbc'
    assert run('name',make(d/'a.zip',('__MACOSX/x.gba',rom),('._x.gba',rom),('readme.txt',rom),('tiny.gba',b'x'),('x.gb',other)))=='a.gb'
    assert run('name',make(d/'a.zip',('readme.txt',rom)))=='-'
    # Unpacked under the ROM inside's name, once; another game's copy replaces it.
    z=make(d/'one.zip',('Inside.nds',rom));out=Path(run('unzip',z,data))
    assert out==data/'unzipped'/crc(rom)/'Inside.nds' and out.read_bytes()==rom
    stamp=os.stat(out).st_mtime_ns
    assert Path(run('unzip',z,data))==out and os.stat(out).st_mtime_ns==stamp,'unpacked again'
    out.write_bytes(b'short');assert Path(run('unzip',z,data)).read_bytes()==rom,'a partial copy was kept'
    again=Path(run('unzip',make(d/'again.zip',('Inside.nds',rom)),data))
    assert again==out and again.read_bytes()==rom and [p.name for p in again.parent.iterdir()]==['Inside.nds']
    out=again
    two=Path(run('unzip',make(d/'two.zip',('Inside.gb',other)),data))
    assert two.read_bytes()==other and not out.exists() and not out.parent.exists()
    # A ROM beside the zip with the copy's base name marks it (pd_save_path then leaves a save of that base name alone).
    (d/'Inside.gba').write_bytes(rom)
    same=Path(run('unzip',make(d/'two.zip',('Inside.gb',other)),data));assert same.with_suffix('.zip').exists()
    (d/'Inside.gba').unlink();run('unzip',d/'two.zip',data);assert not same.with_suffix('.zip').exists()
    # The name saves go by (the app's RomArchive.saveNames): the ROM inside's, unless only the zip's own has a save.
    saves=d/'saves';saves.mkdir();sd=d/'sd';sd.mkdir()
    pack=make(sd/'Pack.zip',('in/FireRed.gba',rom))
    assert run('savename',pack,saves)=='FireRed.gba' and run('savename',d/'loose.gba',saves)=='loose.gba'
    assert Path(run('unzip',pack,data,saves)).name=='FireRed.gba'
    for own in ('Pack.sav','Pack.gba.sav'):
        (saves/own).write_bytes(b'x');assert run('savename',pack,saves)=='Pack.gba',own
        assert Path(run('unzip',pack,data,saves)).name=='Pack.gba',own
        (saves/'FireRed.sav').write_bytes(b'x');assert run('savename',pack,saves)=='FireRed.gba',own
        (saves/'FireRed.sav').unlink();(saves/own).unlink()
    # Pack.sav could be a Pack.gb's beside it: not this game's, so the copy is marked when it goes by Pack.
    (saves/'Pack.sav').write_bytes(b'x');(sd/'Pack.gb').write_bytes(other)
    assert run('savename',pack,saves)=='FireRed.gba'
    (saves/'Pack.gba.sav').write_bytes(b'x');out=Path(run('unzip',pack,data,saves))
    assert out.name=='Pack.gba' and (out.parent/'Pack.zip').exists()
    for f in ('Pack.sav','Pack.gba.sav','Pack.gb'):(saves/f if f.endswith('.sav') else sd/f).unlink()
    # A zip named like a ROM: FireRed.gba.zip goes by FireRed.gba (FireRed.sav, FireRed.gba.sav), X.gb.zip by X.gb too.
    fr=make(sd/'FireRed.gba.zip',('fr.gba',rom))
    assert run('savename',fr,saves)=='fr.gba'
    (saves/'FireRed.sav').write_bytes(b'x');assert run('savename',fr,saves)=='FireRed.gba';(saves/'FireRed.sav').unlink()
    xgb=make(sd/'X.gb.zip',('in.gbc',other))
    (saves/'X.gb.sav').write_bytes(b'x');assert run('savename',xgb,saves)=='X.gb.gbc';(saves/'X.gb.sav').unlink()
    (saves/'X.sav').write_bytes(b'x');assert run('savename',xgb,saves)=='X.gbc';(saves/'X.sav').unlink()
    # An inside name no folder can hold leaves the zip's.
    assert run('savename',make(sd/'odd.zip',('a:b.gba',rom)),saves)=='odd.gba'
    for f in sd.glob('*.zip'):f.unlink()
    # A name inside never leaves the folder, or becomes a file's at all; a comment as long as they get.
    run('crc',make(d/'long.zip',('Long.gba',other),comment=b'x'*65535));assert run('name',d/'long.zip')=='long.gba'
    for name in ('../../evil.gba','..\\..\\evil.gba','/abs/evil.gba'):
        assert run('name',make(d/'evil.zip',(name,rom)))=='evil.gba',name
        assert Path(run('unzip',d/'evil.zip',data)).parent==data/'unzipped'/crc(rom),name
    # Damaged: a flipped byte (CRC32), a cut file, an encrypted entry, a size that lies.
    z=make(d/'ok.zip',('Ok.gba',rom),how=zipfile.ZIP_STORED);raw=bytearray(z.read_bytes())
    bad=bytearray(raw);bad[1000]^=0xff;(d/'bad.zip').write_bytes(bad)
    assert run('crc',d/'bad.zip')=='-' and run('unzip',d/'bad.zip',data)=='-'
    assert not list(data.rglob('*.part')),'a failed copy was left behind'
    (d/'cut.zip').write_bytes(raw[:len(raw)//2]);assert run('name',d/'cut.zip')=='-'
    cd=raw.rindex(b'PK\1\2')
    enc=bytearray(raw);enc[cd+8]|=1;(d/'enc.zip').write_bytes(enc);assert run('name',d/'enc.zip')=='-'
    lie=bytearray(make(d/'lie.zip',('Lie.gba',rom)).read_bytes());cd=lie.rindex(b'PK\1\2')
    lie[cd+24:cd+28]=(4096).to_bytes(4,'little');(d/'lie.zip').write_bytes(lie);assert run('crc',d/'lie.zip')=='-'
    src=make(d/'fuzz.zip',('Fuzz.gba',rom[:5000]),('x.gb',other)).read_bytes()
    for i in range(300):
        f=bytearray(src)
        for _ in range(rnd.randrange(1,8)):f[rnd.randrange(len(f))]=rnd.randrange(256)
        (d/'f.zip').write_bytes(f[:rnd.randrange(1,len(f)+1)] if i%3==0 else f)
        run('crc',d/'f.zip');run('head',d/'f.zip')
    (d/'f.zip').write_bytes(b'');assert run('name',d/'f.zip')=='-'
print('rom zip ok')
