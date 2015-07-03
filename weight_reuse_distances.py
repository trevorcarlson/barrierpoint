#!/usr/bin/env python

import os, sys, gzip, glob, re, jobs

def find_files(count = 1):
  successfiles = []
  for inputsize in jobs.inputsizes:
    for ncores in jobs.ncores:
      gtype = '%s-%s' % (inputsize, ncores)
      globpath = '/home/tcarlson/prog/benchmarks/results/output-instrumented/elvis1-performance/*%s' % (gtype)
      #print globpath
      for f in glob.glob(globpath):
	found_all=True
	#print 'glob=', f
	for ttype,bbfile in (('barrier_reuse_distance','barrier_reuse_distance-default-1/barrier_reuse_distance.txt.bb.gz'),):
	  p=os.path.join(f,'%s-default-1/success' % (ttype))
	  if not os.path.isfile(p):
	    #print 'path not found=', p
	    found_all = False
	  else:
	    #print 'PATH FOUND'
            pass
	if found_all:
          for n,d in ((1,5),(1,2)):
            dir = 'barrier_reuse_distance-%d_%d-default-1' % (n,d)
            if not os.path.isfile(os.path.join(f,dir,'success')):
	      successfiles.append((f,(n,d),dir))
              if len(successfiles) >= count:
                return successfiles
  return successfiles

def run(count = float('Inf')):

  successfiles = find_files(count = count)

  print 'Weight Reuse Distances: Found %d new results to weight.' % len(successfiles)

  for f,(num,denom),dir in successfiles:

    print f, num, denom, dir

    powval = float(num) / float(denom)

    #bbv_fn=os.path.join(f,'barrier_bbv-default-1/barrier_bbv.txt_count.gz')
    reuse_fn=os.path.join(f,'barrier_reuse_distance-default-1/barrier_reuse_distance.txt')
    #insn_fn=os.path.join(f,'barrier_bbv-default-1/barrier_bbv.txt_insncount.gz')
    out_dir=os.path.join(f,dir)
    out_fn=os.path.join(out_dir,'barrier_reuse_distance.txt.bb.gz')

    try:
      os.makedirs(out_dir)
    except OSError:
      pass

    #if not os.path.isfile(bbv_fn):
    #  print "Combine: Warning: Unable to file a file:", bbv_fn
    #  continue
    if not os.path.isfile(reuse_fn):
      print "Combine: Warning: Unable to file a file:", reuse_fn
      continue
    #if not os.path.isfile(insn_fn):
    #  print "Combine: Warning: Unable to file a file:", bbv_fn
    #  continue

    with open(reuse_fn, 'r') as fi:
      maxhlen = 0
      current_thread = 0
      thread_data = []
      barrier_data = []
      for line in fi:
        #m = re.search(r'Th:\s*(\d+) b:\s*(\d+)', line)
        m = re.findall(r'\d+', line)
        #print m
        m = map(int, m)
        th=m[0]
        bar=m[1]
        data=m[2:]

        # Skip the first line because it contains the reuse data for pre-ROI
        if bar == 0:
          continue
        else:
          bar = bar-1

        maxhlen = max(maxhlen, len(data))
        # Weight the data according to the reuse distance
        for i,d in enumerate(data[:]):
          # Reuse distance increases by a power of 2
          if i != 0:
            rd = 1 << i;
          else:
            rd = 0
          data[i] = int(d * pow(rd,powval))
 
        if current_thread == th:
          barrier_data.append(data)
        else:
          thread_data.append(barrier_data)
          barrier_data = []
          barrier_data.append(data)
          current_thread = th
        #print th, b, data

    # add the last barrier_data to the thread data
    thread_data.append(barrier_data)
    barrier_data = []

    #print thread_data

    out = gzip.GzipFile(out_fn, 'w')
    for b in range(bar+1):
      out.write('T')
      for t in range(th+1):
        #print b, t
        for idx,h in enumerate(thread_data[t][b]+([0]*(maxhlen-len(thread_data[t][b])))):
          if h != 0:
            out.write(':%d:%d ' % (1+idx+(t*maxhlen), h))
      out.write('\n')
    out.close()
    os.system('touch "%s"' % (os.path.join(out_dir,'success')))

if __name__ == '__main__':
  run()
