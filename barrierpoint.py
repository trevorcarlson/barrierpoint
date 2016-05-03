#!/usr/bin/env python

import sys, os, subprocess, getopt, itertools, gzip, glob, re, inspect
import combine_simpoint_data
import genbarriercmd

def mkdir_s(path):
  try:
    os.makedirs(path)
  except OSError:
    pass 

def ex(cmd, validate = True):
  proc = subprocess.Popen([ 'bash', '-c', cmd ])
  proc.communicate()
  if validate and proc.returncode != 0:
    raise RuntimeError("returncode=%d for [%s]" % (proc.returncode, cmd))

def ex_log(cmd, config, validate = True):
  cmd += ' 2>&1 | tee -a %(log_file)s' % config
  ex(cmd, validate)

def log(config, *args): 
  #frm = inspect.stack()[1]
  #mod = inspect.getmodule(frm[0])
  fun = sys._getframe(1).f_code.co_name
  ex_log('echo "[%s()] %s"' % (fun, ' '.join(map(str, args))), config)

def make_mt_pinball(config):
  mkdir_s(os.path.dirname(config['whole_basename']))
  # pages_early, whole_image and whole_stack are not working properly
  cmd = '%(pin_bin)s %(pin_options)s -t %(pin_kit)s/extras/pinplay/bin/intel64/pinplay-driver.so -log -log:basename "%(whole_basename)s" -log:compressed bzip2 -log:pages_early 1 -log:whole_image 1 -log:whole_stack 0 -- %(app_cmd)s' % config
  ex_log(cmd, config)

def gen_bbv(config):
  mkdir_s(os.path.dirname(config['bbv_basename']))
  cmd = '%(pin_bin)s %(pin_options)s -reserve_memory %(whole_basename)s.address -t %(pintool_bbv)s -replay -replay:basename %(whole_basename)s -roi 1 -o %(bbv_basename)s -- %(pin_kit)s/extras/pinplay/bin/intel64/nullapp' % config
  ex_log(cmd, config)

def gen_ldv(config):
  mkdir_s(os.path.dirname(config['ldv_basename']))
  cmd = '%(pin_bin)s %(pin_options)s -reserve_memory %(whole_basename)s.address -t %(pintool_reuse_distance)s -replay -replay:basename %(whole_basename)s -roi 1 -o %(ldv_basename)s -- %(pin_kit)s/extras/pinplay/bin/intel64/nullapp' % config
  ex_log(cmd, config)

def combine_to_sv(config):
  basename = os.path.dirname(os.path.dirname(config['sv_basename']))
  combine_simpoint_data.run(basename)

def cluster(config):
  simpointdata = genbarriercmd.gensimpointdata(bbfile=config['sv_basename'])

  config['spdataraw'] = simpointdata
  
  id2index = [None for i in range(max(simpointdata['id'])+1)]
  for i,d in enumerate(simpointdata['id']):
    id2index[d] = i

  simpoint_bb_map = [ simpointdata['simpoints'][id2index[i]] for i in simpointdata['labels'] ]
  config['spbbmap'] = simpoint_bb_map

def gen_pinball_regions(config):
  mkdir_s(os.path.dirname(config['pinball_regions_file_in']))
  cmd = '%(pin_bin)s %(pin_options)s -reserve_memory %(whole_basename)s.address -t %(pintool_icount)s -replay -replay:basename %(whole_basename)s -icountfile %(pinball_regions_file_in)s -- %(pin_kit)s/extras/pinplay/bin/intel64/nullapp' % config
  ex_log(cmd, config)

def gen_pinballs(config, gen_warmup = True):

  try:
    os.remove(config['bp_regions_file_in'])
  except OSError:
    pass
  try:
    os.remove(config['bp_regions_file_out'])
  except OSError:
    pass

  # Determine scaling factors for barrierpoints
  with gzip.GzipFile(config['bbv_basename'] + '_insncount.gz', 'r') as f:
    s = f.read()
  s = filter(lambda x:x, s.split('\n'))
  log(config, s)
  ninsns_per_region = [ sum(map(int,i.split(','))) for i in s ]
  log(config, 'insns', ninsns_per_region)
  log(config, 'spbbmap', config['spbbmap'])
  scaling_factors = [ (1.0+((float(ninsns_per_region[i]) - ninsns_per_region[sp]) / ninsns_per_region[sp])) for i,sp in enumerate(config['spbbmap']) ]
  log(config, 'scaling', scaling_factors)

  bp_to_scaling = [ 0.0 for i in range(len(config['spbbmap']))]
  for i,sp in enumerate(config['spbbmap']):
    bp_to_scaling[sp] += scaling_factors[i]

  log(config, 'bp_to_scaling', bp_to_scaling)

  # Determine significant barrierpoints
  total_insns = sum(ninsns_per_region)
  sig_bp = []
  for sp in sorted(config['spdataraw']['simpoints']):
    percent_sp = (float(ninsns_per_region[sp])*bp_to_scaling[sp])/float(total_insns)
    if percent_sp > 0.005:
      sig_bp.append(sp)
    log(config, (sp, percent_sp))
  log(config, 'sig bp =', sorted(config['spdataraw']['simpoints']), sig_bp)
  config['sig_bp'] = sig_bp

  with file(config['pinball_regions_file_in'], 'r') as f:
    s = f.read()
  s = s.split('\n')
  # Replace the scaling factor in the string for the new scaling factor
  def repl_1_end(st,new_scaling):
    st = st.split(',')
    st[-1] = '%.5f' % new_scaling
    log(config, 'repl', st, new_scaling)
    return ','.join(st)
  # The built-in SimPoint naming conventions do not allow for scaling factors greater than 1.0
  #rfile = [ repl_1_end(s[i],bp_to_scaling[i]) for i in sorted(config['spdataraw']['simpoints']) ]
  rfile = [ s[i] for i in sorted(config['sig_bp']) ]
  log(config, 'rfile', rfile)

  if gen_warmup:
    rfilewarm = [ s[i] for i in sorted(config['sig_bp']) ]
    rfilewarm_new = []
    for l in rfilewarm:
      ls = l.split(',')
      rfilewarm_new.append(','.join(ls[0:3] + ['1'] + [ls[3]] + ['0.00001']))
    rfile = list(itertools.chain.from_iterable(zip(rfilewarm_new, rfile)))
    #below doesn't seem to work as Pin's regions selection does not correctly find overlapping regions, causing resulting in a truncated run 
    #rfile = rfilewarm_new + rfile

  mkdir_s(os.path.dirname(config['barrierpoint_basename']))
  with file(config['barrierpoint_basename']+'.bp_scaling.txt', 'w') as f:
    f.write('\n'.join(map(lambda x:str(x[0])+','+str(x[1]),([(i,bp_to_scaling[i]) for i in sorted(config['sig_bp'])]))))

  with file(config['bp_regions_file_in'], 'w') as f:
    f.write('\n'.join(rfile))

  for itr in itertools.count():
    # pages_early, whole_image and whole_stack are not working properly
    cmd = '%(pin_bin)s %(pin_options)s -reserve_memory %(whole_basename)s.address -t %(pin_kit)s/extras/pinplay/bin/intel64/pinplay-driver.so -replay -replay:basename %(whole_basename)s -log -log:basename "%(barrierpoint_basename)s" -log:compressed bzip2 -log:regions:verbose 1 -log:regions:in %(bp_regions_file_in)s -log:region_id 1 -log:regions:out %(bp_regions_file_out)s -log:pages_early 1 -log:whole_image 1 -log:whole_stack 0 -- %(pin_kit)s/extras/pinplay/bin/intel64/nullapp' % config
    ex_log(cmd, config)

    # Are there any regions that we not processed?
    with file(config['bp_regions_file_out']) as f:
      if f.read():
        # move the output file to the input file and rerun
        os.rename(config['bp_regions_file_in'], config['bp_regions_file_in']+str(itr))
        os.rename(config['bp_regions_file_out'], config['bp_regions_file_in'])
      else:
        # When done, break
        break

def get_ncores(config):

  whole_basename = config['whole_basename']
  max_ncores = 0
  for fn in glob.glob(whole_basename+'.*.sel*'):
    m = re.search(r'\.(\d+)\.sel', fn)
    if not m:
      log(config, 'WARNING:', fn, 'did not match')
    else:
      max_ncores = max(max_ncores, int(m.groups()[0]))
  return max_ncores+1

import xml.etree.cElementTree as ET
def get_address(filename = 'true.procinfo.xml', symbolname = 'GOMP_parallel_start'):

  tree = ET.parse(filename)
  root = tree.getroot()

  for sym in root.iter('symbol'):
    if sym.attrib['name'] == symbolname:
      return sym.attrib['base']

# Evaluate the pinballs with Sniper
def run_sniper(config):

  ncores = get_ncores(config)

  if ncores > 1:
    is_mt = True
  else:
    is_mt = False

  sniper_root = config['sniper_root']
  simpoints = map(str,sorted(config['spdataraw']['simpoints']))
  outputdir = config['sniper_output_dir']

  pinballs_all = {'warmup': [], 'detailed': []}
  for typ, percent in (('warmup','0-00001'), ('detailed','1-00000')):
    for sp in simpoints:
      fnglob = config['barrierpoint_basename'] + ('*r%(sp)s_*%(percent)s*.address' % locals())
      log(config, 'fnglob =', fnglob)
      r = glob.glob(fnglob)

      if len(r) < 1 or len(r) > 1:
        log(config, 'Warning, length of r != 1!', len(r))
        pinballs_all[typ].append('')
      else:
        pinballs_all[typ] += [os.path.splitext(r[0])[0]]

  log(config, 'pinballs', pinballs_all)

  pinballs = pinballs_all['detailed']
  warmup_pinballs = pinballs_all['warmup']

  # Process all of the pinballs for now, changing the exit condition
  new_pinballs = []
  for pinball in pinballs:
    fileName, fileExtension = os.path.splitext(pinball)
    for sel_file in glob.glob(fileName+'*.sel.bz2') + glob.glob(fileName+'*.reg.bz2'):

      ex_log('bunzip2 "%s"' % sel_file, config)

      # Update the file as appropriate
      new_sel_file = os.path.splitext(sel_file)[0]
      log(config, new_sel_file)
      with open (new_sel_file, "r") as myfile:
        d = myfile.read().split('\n')
        log(config, 'd', d[-2])
        l = d[-2].split(' ')
        log(config, 'found', l[1])
        l[1] = str(int(l[1]) * 2)
        log(config, 'converted to', l[1])
        l = ' '.join(l)
        log(config, 'new line', l)
        d[-2] = l
      with open (new_sel_file, "w") as myfile:
        myfile.write('\n'.join(d))
      
      ex_log('bzip2 "%s"' % os.path.splitext(sel_file)[0], config)

  mkdir_s(outputdir)

  assert(len(simpoints) == len(pinballs) and len(pinballs) == len(warmup_pinballs))

  for simpoint, pinball, warmup_pinball in zip(simpoints, pinballs, warmup_pinballs):
    if not pinball or not warmup_pinball:
      log(config, 'Skipping', pinball, warmup_pinball)
      continue
    # TODO: exit on  GOMP_parallel_start (get_address())
    exit_on_address = get_address(pinball+'.procinfo.xml')
    sniper_output_dir = os.path.join(outputdir, simpoint)
    sniper_cmd = '%(sniper_root)s/run-sniper -d %(sniper_output_dir)s --pinballs=%(pinball)s -sipctrace -v -n %(ncores)s' % locals()
    log(config, 'TODO: sniper_cmd = [', sniper_cmd, ']')
    # TODO: add support to Sniper to run multi-threaded pinballs (See is_mt variable above). Requirements are emulation of syscalls
    # TODO: add support to Sniper to run a warmup pinball.
    # TODO: instead of a warmup pinball, ass support for replaying of fast BarrierPoint-based LRU-based warmup snapshots
    #ex_log(sniper_cmd, config)

  if config['run_sniper_full']:
    full_run_output_dir = os.path.join(config['sniper_output_dir'], 'full')
    app_cmd = config['app_cmd']
    sniper_cmd = '/scratch/tcarlson/prog/sniper-master/run-sniper --roi -n %(ncores)s -d %(full_run_output_dir)s -- %(app_cmd)s' % locals()
    ex_log(sniper_cmd, config)
  else:
    log(config, 'NOTE: Skipping full Sniper run')

def evaluate(config):

  log(config, 'TODO: rebuild/eval the runtime and compare with the entire run')

def run(prefix, app_cmd):

  config = {}
  config['app_cmd'] = ' '.join(app_cmd)
  config['whole_basename'] = 'work/wholeprogram/pb'
  config['bbv_basename'] = 'work/barrier_bbv-default-1/barrier_bbv.txt'
  config['ldv_basename'] = 'work/barrier_reuse_distance-default-1/barrier_reuse_distance.txt'
  config['sv_basename'] = 'work/barrier_combine-default-1/combine.bb.gz'
  config['pinball_regions_file_in'] = 'work/pintool.in.csv'
  config['bp_regions_file_in'] = 'work/pintool_bp.in.csv'
  config['bp_regions_file_out'] = 'work/pintool_bp.out.csv'
  config['pinball_regions_file_out'] = 'work/pintool.out.csv'
  config['barrierpoint_basename'] = 'work/barrierpoint/pb'
  config['log_file'] = 'work/log.txt'

  config['pin_kit'] = os.getenv('PIN_ROOT') or '/scratch/tcarlson/prog/sniper-barrierpoint/pin_kit'
  config['pin_options'] = '-xyzzy -ifeellucky'

  config['sniper_root'] = os.getenv('SNIPER_ROOT') or '/scratch/tcarlson/prog/sniper-barrierpoint'
  config['sniper_output_dir'] = 'work/sniper'

  config['benchmarks_root'] = os.getenv('BENCHMARKS_ROOT') or '/scratch/tcarlson/prog/benchmarks'
  config['pintool_bbv'] = os.getenv('PINTOOL_BBV') or ('%(benchmarks_root)s/tools/instrumentation/tool_barrier_bbv/tool_barrier_bbv.so' % config)
  config['pintool_reuse_distance'] = os.getenv('PINTOOL_REUSE_DISTANCE') or ('%(benchmarks_root)s/tools/instrumentation/tool_barrier_reuse_distance/tool_barrier_reuse_distance.so' % config)
  config['pintool_icount'] = os.getenv('PINTOOL_ICOUNT') or ('%(benchmarks_root)s/tools/instrumentation/tool_barrier_icount/tool_barrier_icount.so' % config)

  config['run_sniper_full'] = False

  mkdir_s('work')

  # Validation
  if not os.path.isfile(os.path.join(config['pin_kit'],'pin')):
    raise RuntimeError('Cannot find pin kit at [%s]' % config['pin_kit'])
  for pintool in ('pintool_bbv', 'pintool_reuse_distance', 'pintool_icount'):
    if not os.path.isfile(config[pintool]):
      raise RuntimeError('Cannot file pintool [%s], set the %s environment variable appropriately' % (config[pintool], pintool.upper()))

  if os.path.isfile(os.path.join(config['pin_kit'],'pin.sh')):
    config['pin_bin'] = os.path.join(config['pin_kit'],'pin.sh')
  else:
    config['pin_bin'] = os.path.join(config['pin_kit'],'pin')

  log(config, 'Running command:', app_cmd)

  make_mt_pinball(config)
  
  gen_bbv(config)
  gen_ldv(config)
  combine_to_sv(config)

  cluster(config)

  log(config, 'Barrier points selected: ', sorted(config['spdataraw']['simpoints']))

  gen_pinball_regions(config)
  gen_pinballs(config)

  if True:
    run_sniper(config)
    evaluate(config)


if __name__ == '__main__':

  def usage(rc = 1):
    print 'barrierpoint.py [--prefix=<prefix>] [-h|--help] -- <application> <app-parameters ...>'
    sys.exit(rc)

  prefix = None

  try:
    opts, args = getopt.getopt(sys.argv[1:], 'h', [ 'help', 'prefix=' ])
  except getopt.GetoptError, e:
    # print help information and exit:
    print e
    usage()
  for o, a in opts:
    if o == '-h' or o == '--help':
      usage(0)
    if o == '--prefix':
      prefix = a

  run(prefix, args)
