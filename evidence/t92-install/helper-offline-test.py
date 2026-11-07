import os, subprocess, sys, tempfile
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
src=open(r'D:/TF2_Native_Worktrees/t92-install/install-cli-contract-check.sh',encoding='utf-8').read()
start=src.index('cat > "$HELPER" <<\'PY\''); start=src.index('\n',start)+1
end=src.index('\nPY\n',start)
helper=os.path.join(tempfile.gettempdir(),'t92-helper.py')
open(helper,'w',encoding='utf-8',newline='\n').write(src[start:end])
base=tempfile.mkdtemp(prefix='t92test-')
inst=os.path.join(base,'验收 路径 空格','安装 目录'); os.makedirs(inst, exist_ok=True)
man=os.path.join(inst,'m.txt')
win=inst.replace('/','\\')
open(man,'wb').write(('package=tf2_demo_native\ninstallDirectory='+win+'\n').encode('utf-8'))
print('manifest value =', win)
def run(mode,*extra):
    r=subprocess.run([sys.executable, helper, mode, man, *extra], capture_output=True, text=True, encoding='utf-8', errors='replace')
    return r.stdout.strip()
print('--- check (expect equals_utf8=yes, cp936_reads_as_path=no) ---'); print(run('check', win))
print('--- legacy ---'); print(run('legacy'))
print('--- check after (expect valid_utf8=no, cp936_reads_as_path=yes) ---'); print(run('check', win))