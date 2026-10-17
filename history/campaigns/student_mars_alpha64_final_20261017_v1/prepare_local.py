from pathlib import Path
import shutil
W=Path(__file__).resolve().parent
C=Path('C:/Users/25854/Documents/Codex/2026-10-03/jian/outputs/student_mars_freq_a32_formal_20261015_v1')
for name in ['run_formal.py','validate_partition.py']:
 s=(C/name).read_text()
 if name=='run_formal.py':
  for a,b in [("'alpha':32","'alpha':64"),('alpha=32','alpha=64'),(",32])",",64])"),("ident['alpha']==32","ident['alpha']==64"),("x['alpha']=='32'","x['alpha']=='64'")]:s=s.replace(a,b)
  s=s.replace("if __name__=='__main__':", "if __name__=='__unused_old_main__':")
  name='formal_support.py'
 else:
  s=s.replace("scalar(b,'<i')==32","scalar(b,'<i')==64").replace('len(bb)<32','len(bb)<64').replace('x>=32','x>=64').replace('x<32','x<64').replace("scalar(f,'<i')==32","scalar(f,'<i')==64").replace("'NYC':1387,'BAY':1246,'COL':1025,'FLA':1890","'NYC':1030,'BAY':910,'COL':774,'FLA':1284").replace("'alpha':32","'alpha':64")
 (W/name).write_text(s,encoding='utf-8',newline='\n')
