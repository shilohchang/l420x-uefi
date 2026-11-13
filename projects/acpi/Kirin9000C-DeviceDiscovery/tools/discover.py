#!/usr/bin/env python3
"""Read-only L420X FDT inventory and conservative ACPI diagnostic SSDT generator.
No firmware or disk access. The generated SSDTs default to hidden devices.
"""
import argparse, csv, json, re, struct, collections
from pathlib import Path

PATS = {
 'storage':r'ufs|mmc|sdhci|sdio|sdcard|emmc|nvme|flash',
 'pcie':r'pcie|pci@|pci-express|pcie-kport',
 'input':r'hid-over-i2c|hid_i2c|keyboard|keypad|touchpad|touchscreen|gpio-key|mouse|echub|thp|fingerprint',
 'network':r'ethernet|wifi|wlan|wireless|hisi-hi110|hi110|bluetooth|bt_|gbe|gm ac',
 'i2c_spi':r'i2c|spi@|spi-master|i3c',
 'usb':r'usb|dwc3|xhci|ehci|phy-usb',
 'display':r'dpu|display|mipi|dsi|dss|dp_|drm|hdmi|blpwm|panel|backlight',
 'audio':r'audio|sound|codec|hifi|asp_|i2s|mic|speaker',
 'power':r'pmic|regulator|battery|charger|power|thermal|clock|crg|reset|fan|pwm',
 'gpio':r'gpio|pinctrl|pinmux',
 'dma_iommu':r'smmu|iommu|dma|its@|gic|interrupt-controller',
 'sensor':r'sensorhub|gyroscope|accel|light|proximity|camera|isp|magnet|vibrator',
 'compute':r'gpu|mali|npu|jpeg|video|vdec|venc',
 'security':r'tee|hisee|crypto|secure|fingerprint|watchdog',
 'other':r'.',
}
PRIORITY={'storage':0,'pcie':1,'input':2,'network':3,'i2c_spi':4,'gpio':5,'dma_iommu':6,'usb':7,'display':8,'audio':9,'sensor':10,'compute':11,'power':12,'security':13,'other':14}

def cells(b):
 if len(b)%4: return []
 return list(struct.unpack('>'+ 'I'*(len(b)//4),b))
def stringlist(b):return [v.decode('utf-8','replace') for v in b.split(b'\0') if v]
def val(c):
 out=0
 for a in c:out=(out<<32)|a
 return out

def parse(data):
 if len(data)<40 or data[:4]!=bytes.fromhex('d00dfeed'):raise ValueError('Invalid DTB')
 total,so,stro,_,_,_,_,ss,st=struct.unpack_from('>9I',data,4)
 # header fields: totalsize, off_dt_struct, off_dt_strings, off_mem_rsvmap, version, last_comp_version, boot_cpuid_phys, size_dt_strings, size_dt_struct
 if total>len(data) or so+st>total or stro+ss>total:raise ValueError('Invalid DTB offsets')
 pool=data[stro:stro+ss]
 cur=so; stack=[]; nodes=[]
 while cur<so+st:
  tok=struct.unpack_from('>I',data,cur)[0];cur+=4
  if tok==1:
   end=data.index(b'\0',cur,so+st);name=data[cur:end].decode('utf-8','replace');cur=(end+4)&~3
   parent=stack[-1] if stack else None
   path=(parent['path'].rstrip('/') if parent else '')+'/'+name
   n={'path':path,'name':name,'parent':parent,'properties':{}};nodes.append(n);stack.append(n)
  elif tok==2:stack.pop()
  elif tok==3:
   length,off=struct.unpack_from('>II',data,cur);cur+=8
   end=pool.index(b'\0',off);key=pool[off:end].decode('utf-8','replace')
   stack[-1]['properties'][key]=data[cur:cur+length];cur=(cur+length+3)&~3
  elif tok==4:pass
  elif tok==9:break
  else:raise ValueError(f'Invalid token {tok}')
 return nodes

def property_cells(n,key):return cells(n['properties'].get(key,b''))
def numeric(n,key,default=None):
 v=property_cells(n,key);return v[0] if v else default

def reg_entries(n):
 p=n['parent'];raw=property_cells(n,'reg');out=[]
 if not p or not raw:return out
 a=numeric(p,'#address-cells',2);s=numeric(p,'#size-cells',1)
 if a is None or s is None or a>4 or s>4 or (a+s)==0 or len(raw)%(a+s):return out
 for i in range(0,len(raw),a+s):
  addr=val(raw[i:i+a]);size=val(raw[i+a:i+a+s]);out.append({'raw_address':addr,'size':size})
 return out

def status(n):
 cur=n
 while cur:
  s=stringlist(cur['properties'].get('status',b''))
  if s and s[0].lower() not in ('ok','okay'):return s[0]
  cur=cur['parent']
 return 'okay'

def interrupts(n,phandle):
 p=n['properties'];src=n
 while src and 'interrupt-parent' not in src['properties']:src=src['parent']
 controller=phandle.get(numeric(src,'interrupt-parent')) if src else None
 r=property_cells(n,'interrupts'); out=[]
 count=numeric(controller,'#interrupt-cells',3) if controller else 3
 if count and len(r)%count==0:
  for i in range(0,len(r),count):
   a=r[i:i+count];d={'cells':a,'controller':controller['path'] if controller else None}
   if count==3 and controller and any('gic' in x.lower() for x in stringlist(controller['properties'].get('compatible',b''))):
    # GIC_SPI=0, GIC_PPI=1
    d['candidate_gsi']=a[1]+(32 if a[0]==0 else 16 if a[0]==1 else 0)
    d['trigger_flags']=a[2]
   out.append(d)
 return out

def category(n):
 p=n['properties'];hay=(n['path']+' '+ ' '.join(stringlist(p.get('compatible',b'')))).lower()
 for k,regex in PATS.items():
  if re.search(regex,hay):return k
 return 'other'

def collect(nodes):
 phandle={numeric(n,'phandle'):n for n in nodes if numeric(n,'phandle') is not None}
 result=[]
 for n in nodes:
  p=n['properties'];compat=stringlist(p.get('compatible',b''));regs=reg_entries(n);intr=interrupts(n,phandle)
  cat=category(n);state=status(n)
  result.append({'path':n['path'],'category':cat,'status':state,'compatible':compat,
    'reg':regs,'interrupts':intr,'phandle':numeric(n,'phandle'),
    'clock_refs':property_cells(n,'clocks'), 'reset_refs':property_cells(n,'resets'),
    'power_domain_refs':property_cells(n,'power-domains'),
    'supply_properties':{k:property_cells(n,k) for k in p if k.endswith('-supply')},
    'dma_coherent':'dma-coherent' in p, 'iommus':property_cells(n,'iommus'),
    'parent_path':n['parent']['path'] if n['parent'] else None,
    'keys':sorted(p.keys()),'resource_notes':[]})
 return result

def should_generate(d):
 # Generic discoverable platform devices only. Never synthesize OS storage/PCI root compatibility.
 if d['status']!='okay' or not d['compatible'] or not d['reg']:return False
 if not d['reg'][0]['size'] or d['reg'][0]['size']>0x10000000:return False
 if d['path'].startswith(('/reserved-memory','/memory','/cpus')):return False
 if d['category']=='other':return False
 # child reg is frequently an I2C slave address / SPI CS, NOT MMIO
 if d['parent_path'] and re.search(r'(i2c@|spi@|spmi@|pci@|gpio@)',d['parent_path'].lower()):return False
 if 'gpio' in d['category'] and 'pinmux' in d['path']:return False
 return True

def generate(ds,out):
 cand=[d for d in ds if should_generate(d)]
 # avoid resource collision with existing base platform device, this is only a *diagnostic* plan
 # includes only a stable diagnostic vendor HID, never a class CID, no PNP binding.
 for idx,d in enumerate(cand):d['acpi_debug_id']=f'LTX{idx:04X}'
 groups=collections.defaultdict(list)
 for d in cand:groups[d['category']].append(d)
 for cat,rows in groups.items():
  if cat in ('other',):continue
  lines=['DefinitionBlock ("", "SSDT", 2, "L42DBG", "DTSCAN  ", 0x00000001)','{', '    Scope (\\_SB)', '    {']
  for d in rows:
   idx=int(d['acpi_debug_id'][-4:],16)
   # Windows can see this device only after manually switching disabled to enabled in a copy.
   # Do NOT publish live MMIO _CRS for broad discovery: many regs are auxiliary or translated.
   lines.extend([f'        // {d["path"]}',f'        // Linux compatible: {", ".join(d["compatible"])}',f'        // Raw first reg: 0x{d["reg"][0]["raw_address"]:X} size 0x{d["reg"][0]["size"]:X} (not validated as physical MMIO)', f'        Device (D{idx:03X})', '        {', f'            Name (_HID, "{d["acpi_debug_id"]}")', f'            Name (_UID, {idx})', f'            Name (_DDN, "L420X DT scan {cat}")', '            Method (_STA, 0, NotSerialized) { Return (Zero) }', '        }',''])
  lines+=['    }','}']
  (out/f'SSDT-DISCOVERY-{cat.upper()}.asl').write_text('\n'.join(lines)+'\n')
 return cand

def main():
 ap=argparse.ArgumentParser();ap.add_argument('dtb',type=Path);ap.add_argument('--out',type=Path,default=Path('output'));args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=True)
 nodes=parse(args.dtb.read_bytes());data=collect(nodes)
 for d in data:
  if d['status']=='okay' and d['compatible']:
   if d['reg'] and d['parent_path'] and re.search(r'(i2c@|spi@|spmi@|pci@)',d['parent_path'].lower()):d['resource_notes'].append('reg is likely bus address, not MMIO')
   if d['category'] in ('storage','pcie','i2c_spi','input','dma_iommu'):d['resource_notes'].append('Requires platform-specific Windows driver / dependency analysis')
  if d['iommus']:d['resource_notes'].append('IOMMU/SMMU mapping needs IORT and driver confirmation')
 (args.out/'asl').mkdir(parents=True, exist_ok=True)
 cand=generate(data,args.out/'asl')
 (args.out/'all-devices.json').write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n')
 with (args.out/'devices.csv').open('w',newline='') as f:
  w=csv.writer(f);w.writerow(['category','status','dt_path','compatible','mmio_or_reg_0','size_0','interrupts','acpi_debug_id','notes'])
  for d in sorted(data,key=lambda v:(PRIORITY.get(v['category'],99),v['path'])):
   r=d['reg'][0] if d['reg'] else {}
   w.writerow([d['category'],d['status'],d['path'],'; '.join(d['compatible']),hex(r['raw_address']) if r else '',hex(r['size']) if r else '', ';'.join(str(x.get('candidate_gsi',x['cells'])) for x in d['interrupts']),d.get('acpi_debug_id',''),'; '.join(d['resource_notes'])])
 counts=collections.Counter(x['category'] for x in data if x['status']=='okay' and x['compatible'])
 summary={'total_nodes':len(data),'enabled_compatible_nodes':sum(counts.values()),'category_counts':dict(counts),'diagnostic_ssdt_candidates':len(cand),'diagnostic_ssdt_groups':dict(collections.Counter(x['category'] for x in cand))}
 (args.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
 print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
