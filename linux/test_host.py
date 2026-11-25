"""The real pokedaisy-linux on the diagnostic ROMs (make_diagnostic.py), headless, over all three cores:
test_host.py BINARY CORES_DIR DIAGNOSTICS_DIR. The GB ROM runs on pokedaisy_gbc and writes a battery-save marker,
which has to reach DATA/saves; the GBA ROM runs on pokedaisy_gba (armGBA) and fills its screen red, which has to
reach the window. The DS ROM fills its top screen green. A second run backs the save up, and the game list draws with no ROM given."""
from pathlib import Path
import os,shutil,struct,subprocess,sys,tempfile,time,zipfile,zlib

binary,cores,diag=(Path(a).resolve() for a in sys.argv[1:4])

def png_pixels(path):
    data=path.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n',path
    pos,idat=8,b''
    while pos<len(data):
        n,kind=struct.unpack('>I4s',data[pos:pos+8]);body=data[pos+8:pos+8+n];pos+=12+n
        if kind==b'IHDR':w,h,depth,color=struct.unpack('>IIBB',body[:10])
        if kind==b'IDAT':idat+=body
    assert depth==8 and color in (2,6),(depth,color)
    bpp=3 if color==2 else 4;raw=zlib.decompress(idat);stride=w*bpp;rows=[];prev=bytearray(stride)
    for y in range(h):
        f=raw[y*(stride+1)];line=bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        for i in range(stride):
            a=line[i-bpp] if i>=bpp else 0;b=prev[i];c=prev[i-bpp] if i>=bpp else 0
            if f==1:line[i]=(line[i]+a)&255
            elif f==2:line[i]=(line[i]+b)&255
            elif f==3:line[i]=(line[i]+(a+b)//2)&255
            elif f==4:
                p=a+b-c;pa,pb,pc=abs(p-a),abs(p-b),abs(p-c)
                line[i]=(line[i]+(a if pa<=pb and pa<=pc else b if pb<=pc else c))&255
        rows.append(bytes(line));prev=line
    return w,h,bpp,rows

env=dict(os.environ,POCKET_CORE_DIR=str(cores),SDL_AUDIODRIVER='dummy')
env.setdefault('SDL_VIDEODRIVER','dummy');env.setdefault('SDL_RENDER_DRIVER','software')
def run(data,*args):
    r=subprocess.run([str(binary),'--data',str(data),*args],env=env,capture_output=True,text=True,timeout=60)
    assert r.returncode==0,(args,r.returncode,r.stderr[-2000:])
    return r.stderr

with tempfile.TemporaryDirectory(prefix='pd-host-') as d:
    d=Path(d);(d/'roms').mkdir()
    for rom in ('diagnostic.gb','diagnostic.gba','diagnostic.nds'):shutil.copy(diag/rom,d/'roms'/rom)
    # The GB ROM on the mGBA core: its save marker written to DATA/saves.
    run(d,str(d/'roms/diagnostic.gb'),'--frames','120','--shot',str(d/'gb.png'))
    sav=d/'saves/diagnostic.gb.sav';assert sav.exists() and sav.read_bytes()[:1]==b'\x5a',sav
    w,h,bpp,rows=png_pixels(d/'gb.png');assert len({r for r in rows})>1,'the GB frame is one colour'
    # Run again: the save is backed up first.
    run(d,str(d/'roms/diagnostic.gb'),'--frames','10')
    backups=list((d/'saves/pokedaisy-backups').glob('diagnostic.gb.backup-*.sav'));assert len(backups)==1,backups
    # A save named after the ROM without its extension (another emulator's: pokeblack2.nds -> pokeblack2.sav) is used as it is,
    # for a ROM no other ROM beside it shares the name of.
    shutil.copy(diag/'diagnostic.gb',d/'roms'/'solo.gb')
    (d/'saves/solo.sav').write_bytes(b'\0'*8192)
    run(d,str(d/'roms/solo.gb'),'--frames','120')
    assert (d/'saves/solo.sav').read_bytes()[:1]==b'\x5a','the save named without the ROM extension was not the one written'
    assert not (d/'saves/solo.gb.sav').exists(),'the ROM-named save was made beside the other emulator name'
    # With both names there, the one written last wins.
    (d/'saves/solo.gb.sav').write_bytes(b'\0'*8192);os.utime(d/'saves/solo.sav',(0,0))
    run(d,str(d/'roms/solo.gb'),'--frames','120')
    assert (d/'saves/solo.gb.sav').read_bytes()[:1]==b'\x5a','the save written last did not win'
    (d/'roms/solo.gb').unlink()
    # A zipped ROM plays as the ROM inside, its save named after it (the app's and RetroArch's rule), or after the zip
    # when only the zip's own name has one; the DS core gets it as a file.
    packed=d/'roms'/'Packed (USA).zip'
    with zipfile.ZipFile(packed,'w',zipfile.ZIP_DEFLATED) as z:z.write(diag/'diagnostic.gb','inside.gb')
    run(d,str(packed),'--frames','120')
    assert (d/'saves/inside.sav').read_bytes()[:1]==b'\x5a','the zipped ROM did not run'
    assert [p.name for p in (d/'unzipped').rglob('*') if p.is_file()]==['inside.gb']
    (d/'saves/inside.sav').unlink();(d/'saves/Packed (USA).sav').write_bytes(b'\0'*8192)
    run(d,str(packed),'--frames','120')
    assert (d/'saves/Packed (USA).sav').read_bytes()[:1]==b'\x5a' and not (d/'saves/inside.sav').exists()
    assert [p.name for p in (d/'unzipped').rglob('*') if p.is_file()]==['Packed (USA).gb']
    (d/'saves/Packed (USA).sav').unlink()
    with zipfile.ZipFile(packed,'w',zipfile.ZIP_DEFLATED) as z:z.write(diag/'diagnostic.nds','inside.nds')
    run(d,str(packed),'--frames','90','--shot',str(d/'ds-zip.png'))
    w,h,bpp,rows=png_pixels(d/'ds-zip.png')
    mid=rows[h//2][(w//2)*bpp:(w//2)*bpp+3]
    assert mid[0]<60 and mid[1]>200 and mid[2]<60,mid
    assert [p.name for p in (d/'unzipped').rglob('*') if p.is_file()]==['inside.nds']
    packed.unlink()
    # diagnostic.gb has diagnostic.gba and .nds beside it: diagnostic.sav could be any of theirs, so it's left alone.
    (d/'saves/diagnostic.gb.sav').rename(d/'saves/diagnostic.sav')
    (d/'saves/diagnostic.sav').write_bytes(b'\0'*8192)
    run(d,str(d/'roms/diagnostic.gb'),'--frames','120')
    assert (d/'saves/diagnostic.sav').read_bytes()==b'\0'*8192,'a save another ROM could own was written'
    assert (d/'saves/diagnostic.gb.sav').read_bytes()[:1]==b'\x5a','the ROM-named save was not made'
    (d/'saves/diagnostic.sav').unlink()
    # A Game Boy cartridge with a clock (MBC3, as Crystal's): the clock is kept after the save, mGBA's layout.
    rtc=bytearray((diag/'diagnostic.gb').read_bytes());rtc[0x147]=0x10;rtc[0x14d]=(-sum(rtc[0x134:0x14d])-25)&255
    (d/'roms/clock.gb').write_bytes(rtc)
    run(d,str(d/'roms/clock.gb'),'--frames','120')
    clock=(d/'saves/clock.sav').read_bytes();assert len(clock)==8192+48 and clock[:1]==b'\x5a',len(clock)
    assert any(clock[8192:]),'the clock bytes are empty'
    # Each run moves the clock behind the save: that alone makes no new backup (only the save data counts).
    run(d,str(d/'roms/clock.gb'),'--frames','10');time.sleep(1.1);run(d,str(d/'roms/clock.gb'),'--frames','10')
    kept=list((d/'saves/pokedaisy-backups').glob('clock.backup-*.sav'));assert len(kept)==1,kept
    # A save shared with the firmware's emulators (SHARED SAVES): its file keeps its size and name.
    gb,srm=d/'firmware/gb',d/'retroarch/gb';gb.mkdir(parents=True);srm.mkdir(parents=True)
    env.update(POCKET_ROM_DIRS=str(gb),POCKET_SAVE_DIRS=str(srm))
    shutil.copy(diag/'diagnostic.gb',gb/'Off.gb');run(d,str(gb/'Off.gb'),'--frames','120')
    assert (d/'saves/Off.sav').exists() and not list(srm.iterdir())
    with (d/'host.ini').open('a') as f:f.write('shared_saves=1\n')
    shutil.copy(diag/'diagnostic.gb',gb/'Shared.gb');(srm/'Shared.srm').write_bytes(b'\0'*8192+b'tail')
    run(d,str(gb/'Shared.gb'),'--frames','120')
    shared=(srm/'Shared.srm').read_bytes();assert shared[:1]==b'\x5a' and len(shared)==8196 and shared[-4:]==b'tail',len(shared)
    assert not list((d/'saves').glob('Shared*')) and not list(srm.glob('*.tmp'))
    assert next((d/'saves/pokedaisy-backups').glob('Shared.gb.backup-*.srm')).read_bytes()[:1]==b'\0'
    # A new one is the core's size, and a compressed one isn't opened.
    shutil.copy(diag/'diagnostic.gb',gb/'Next.gb');run(d,str(gb/'Next.gb'),'--frames','120')
    assert len((srm/'Next.srm').read_bytes())==8192
    shutil.copy(diag/'diagnostic.gb',gb/'Rzip.gb');(srm/'Rzip.srm').write_bytes(b'#RZIPv1#'+b'\0'*60)
    r=subprocess.run([str(binary),'--data',str(d),str(gb/'Rzip.gb'),'--frames','10'],env=env,capture_output=True,text=True,timeout=60)
    assert r.returncode!=0 and not (d/'saves/Rzip.sav').exists() and (srm/'Rzip.srm').read_bytes()[:8]==b'#RZIPv1#'
    # A zipped one, and one without a save yet: RetroArch names it after the ROM inside.
    with zipfile.ZipFile(gb/'Zipped.zip','w') as z:z.write(diag/'diagnostic.gb','Zipped inside.gb')
    run(d,str(gb/'Zipped.zip'),'--frames','120')
    assert (srm/'Zipped inside.srm').read_bytes()[:1]==b'\x5a' and len((srm/'Zipped inside.srm').read_bytes())==8192
    assert not (srm/'Zipped.srm').exists()
    # Its clock is RetroArch's file, left alone while it's the same clock; without one it's PokeDaisy's.
    zero=struct.pack('<q',int(time.time())-5*86400)
    (gb/'Clock.gb').write_bytes(rtc);(srm/'Clock.srm').write_bytes(b'\0'*8192);(srm/'Clock.rtc').write_bytes(zero)
    run(d,str(gb/'Clock.gb'),'--frames','120')
    assert len((srm/'Clock.srm').read_bytes())==8192 and (srm/'Clock.rtc').read_bytes()==zero
    (srm/'Clock.rtc').unlink();(srm/'Clock.srm').unlink();run(d,str(gb/'Clock.gb'),'--frames','120')
    assert len((d/'saves/Clock.gb.rtc').read_bytes())==48 and not (srm/'Clock.rtc').exists()
    # PokeDaisy's save, when there is one, is still the one it plays.
    (d/'saves/Shared.gb.sav').write_bytes(b'\0'*8192);before=(srm/'Shared.srm').read_bytes()
    run(d,str(gb/'Shared.gb'),'--frames','120')
    assert (d/'saves/Shared.gb.sav').read_bytes()[:1]==b'\x5a' and (srm/'Shared.srm').read_bytes()==before
    for k in ('POCKET_ROM_DIRS','POCKET_SAVE_DIRS'):del env[k]
    # The GBA ROM on the armGBA core: its grey / olive one-pixel stripes reach the window.
    log=run(d,str(d/'roms/diagnostic.gba'),'--frames','120','--shot',str(d/'gba.png'))
    w,h,bpp,rows=png_pixels(d/'gba.png')
    row=rows[h//2];seen={tuple(row[i:i+3]) for i in range(0,w*bpp,bpp)}
    assert any(r>120 and g>120 and b>120 for r,g,b in seen) and any(r>120 and g>120 and b<60 for r,g,b in seen),(seen,log[-500:])
    # A second screen (a window stands in for it, the Thor's size) has the companion, where a tap picks a tab, and the DS's
    # touch screen, which the COMPANION button or the one under it swaps for the companion. That one is remembered.
    env['POCKET_SECOND_SCREEN']='1240x1080'
    with (d/'host.ini').open('a') as f:f.write('second_screen=1\n')
    def second(rom,bottom=1080,**keys):
        env.update(keys)
        run(d,str(d/'roms'/rom),'--frames','120','--shot',str(d/'two.png'))
        for k in keys:del env[k]
        w,h,bpp,rows=png_pixels(d/'two.png.second.png');assert (w,h)==(1240,1080),(w,h)
        return {tuple(rows[y][x*bpp:x*bpp+3]) for y in range(0,bottom,24) for x in range(0,w,24)}
    for rom in ('diagnostic.gb','diagnostic.gba'):
        party=second(rom);assert len(party)>3,rom
        assert second(rom,POCKET_MOUSE='60:1190:1030:s')!=party,rom
    touch=second('diagnostic.nds',900);assert (255,255,255) in touch and len(touch)<=2,touch
    swapped=second('diagnostic.nds',900,POCKET_KEYS='60:l2:4');assert len(swapped)>3,swapped
    assert second('diagnostic.nds',900,POCKET_KEYS='60:l2:4,90:l2:4')==touch
    assert len(second('diagnostic.nds',900,POCKET_MOUSE='60:620:1005:s'))>3 and len(second('diagnostic.nds',900))>3
    assert second('diagnostic.nds',900,POCKET_MOUSE='60:620:1005:s')==touch
    # The LCD filter is over the touch screen and not over the companion.
    with (d/'host.ini').open('a') as f:f.write('lcd=1\n')
    assert second('diagnostic.nds',900)!=touch and second('diagnostic.gba')==party
    del env['POCKET_SECOND_SCREEN']
    with (d/'host.ini').open('a') as f:f.write('lcd=0\nsecond_screen=0\n')
    # DS maps the ROM from its path, so it also works above the host's 64 MiB buffer limit.
    with (d/'roms/diagnostic.nds').open('r+b') as nds:nds.truncate(65 << 20)
    # Select DS from the sorted list, save a state through the host menu, and return (QUIT asks: CANCEL, then QUIT). In a
    # DATA folder of its own: the library writes the cores' settings (EXPAND) on its first start, which the runs here
    # mustn't get. Its list view (library.txt), not the grid, whose columns follow the window: the three games are its
    # rows, sorted by file name, so two DOWNs reach the DS one.
    lib=d/'library';(lib/'roms').mkdir(parents=True)
    for rom in ('diagnostic.gb','diagnostic.gba','diagnostic.nds'):os.link(d/'roms'/rom,lib/'roms'/rom)
    (lib/'library.txt').write_text('view=0\nsort=0\ngrid=0\n')
    env['POCKET_KEYS']='10:down,20:down,40:a,70:menu,80:down,90:a,100:menu,110:up,120:a,130:down,140:a'
    run(lib,'--frames','65')
    del env['POCKET_KEYS']
    assert (lib/'states/diagnostic.nds.ss').exists(),'DS did not launch from the game list'
    assert not (d/'saves/pokedaisy/settings.ini').exists() or 'aspect=1' not in (d/'saves/pokedaisy/settings.ini').read_text(),\
        "the game list's settings reached the other runs' DATA"
    run(d,str(d/'roms/diagnostic.nds'),'--frames','90','--shot',str(d/'ds.png'))
    w,h,bpp,rows=png_pixels(d/'ds.png')
    mid=rows[h//2][(w//2)*bpp:(w//2)*bpp+3]
    assert mid[0]<60 and mid[1]>200 and mid[2]<60,mid
    # L2 must reach DS Companion even though GB/GBA use that button for host rewind.
    env['POCKET_KEYS']='25:l2'
    run(d,str(d/'roms/diagnostic.nds'),'--frames','55','--shot',str(d/'ds-companion.png'))
    del env['POCKET_KEYS']
    _,_,_,companion=png_pixels(d/'ds-companion.png')
    assert companion!=rows,'L2 did not open the DS Companion'
    env['POCKET_KEYS']='25:r2'
    run(d,str(d/'roms/diagnostic.nds'),'--frames','55','--shot',str(d/'ds-bottom.png'))
    del env['POCKET_KEYS']
    w,h,bpp,bottom=png_pixels(d/'ds-bottom.png')
    assert min(bottom[h//2][(w//2)*bpp:(w//2)*bpp+3])>200,'R2 did not switch to the DS bottom screen'
    for chord in ('r2','l2'):
        env['POCKET_KEYS']=f'25:select:4,26:{chord}:12'
        run(d,str(d/'roms/diagnostic.nds'),'--frames','55','--shot',str(d/'ds-chord.png'))
        del env['POCKET_KEYS']
        _,_,_,chord_rows=png_pixels(d/'ds-chord.png')
        # The host can overlay a rewind-disabled notice. The DS game remains on its top screen.
        r,g,b=chord_rows[3*h//8][(w//2)*bpp:(w//2)*bpp+3]
        assert r<60 and g>100 and b<60,chord
    # The host menu (Escape) with the keys scripted: DOWN to SAVE STATE, A writes the state file.
    env['POCKET_KEYS']='20:menu,30:down,40:a'
    run(d,str(d/'roms/diagnostic.gb'),'--frames','80')
    del env['POCKET_KEYS']
    assert (d/'states/diagnostic.gb.ss').exists(),'SAVE STATE from the host menu wrote nothing'
    # The host's settings are in the companion's SETTINGS (PD_ENV_FRONT_ROWS): the menu's SETTINGS (three rows down: RESUME,
    # SAVE STATE, LOAD STATE, SETTINGS, QUIT) opens it at them, A/B BUTTONS first; A flips it, saved with the host's settings.
    steps=['menu']+['down']*3+['a','a']
    env['POCKET_KEYS']=','.join(f'{20+10*i}:{k}' for i,k in enumerate(steps))
    run(d,str(d/'roms/diagnostic.gb'),'--frames','150')
    del env['POCKET_KEYS']
    assert 'swap_ab=1' in (d/'host.ini').read_text(),(d/'host.ini').read_text()
    # HOTKEYS (CONTROLS: A/B BUTTONS, KEYBOARD, HOTKEYS, so two rows down from where SETTINGS opens) opens the host's own
    # page: REWIND takes L3; SAVE STATE takes R2, which FAST FORWARD then loses.
    ini=(d/'host.ini').read_text();assert 'hk_ff=r2' in ini and 'hk_rewind=l2' in ini,ini
    steps=['menu']+['down']*3+['a']+['down']*2+['a','down','a','l3','down','a','r2']
    env['POCKET_KEYS']=','.join(f'{20+10*i}:{k}' for i,k in enumerate(steps))
    run(d,str(d/'roms/diagnostic.gb'),'--frames',str(40+10*len(steps)))
    del env['POCKET_KEYS']
    ini=(d/'host.ini').read_text()
    assert 'hk_rewind=l3' in ini and 'hk_savestate=r2' in ini and 'hk_ff=off' in ini and 'hk_loadstate=off' in ini,ini
    # The hotkey requires the modifier: Select + R2 now saves the state.
    (d/'states/diagnostic.gb.ss').unlink()
    env['POCKET_KEYS']='30:r2'
    run(d,str(d/'roms/diagnostic.gb'),'--frames','60')
    assert not (d/'states/diagnostic.gb.ss').exists(),'R2 alone saved a state'
    env['POCKET_KEYS']='25:select:12,30:r2'
    run(d,str(d/'roms/diagnostic.gb'),'--frames','60')
    del env['POCKET_KEYS']
    assert (d/'states/diagnostic.gb.ss').exists(),'the SAVE STATE hotkey wrote nothing'
    # No ROM: the game list (its own DATA folder again).
    run(lib,'--frames','5','--shot',str(d/'list.png'));png_pixels(d/'list.png')
    # A ROM no core plays is refused, not crashed on.
    (d/'roms/bad.txt').write_text('x')
    r=subprocess.run([str(binary),'--data',str(d),str(d/'roms/bad.txt'),'--frames','5'],env=env,capture_output=True,text=True,timeout=60)
    assert r.returncode==1 and 'Unsupported ROM' in r.stderr,r.stderr
print('host ok')
