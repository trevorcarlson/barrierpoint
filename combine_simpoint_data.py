#!/usr/bin/env python

import os, sys, gzip, glob, traceback
import getopt

def write_error(s, outdir=None):
  print s
  print traceback.format_exc()
  return

def run(prefix, attr = ''):

  successfiles = ((prefix,attr),)

  for f,attr in successfiles:

    bbv_fn=os.path.join(f,'barrier_bbv-default-1/barrier_bbv.txt_count.gz')
    insn_fn=os.path.join(f,'barrier_bbv-default-1/barrier_bbv.txt_insncount.gz')
    reuse_fn=os.path.join(f,'barrier_reuse_distance%s-default-1/barrier_reuse_distance.txt.bb.gz' % attr)
    out_dir=os.path.join(f,'barrier_combine%s-default-1' % attr)
    out_fn=os.path.join(out_dir,'combine.bb.gz')

    try:
      os.makedirs(out_dir)
    except OSError, e:
      pass # Ignore this error

    if not os.path.isfile(bbv_fn):
      print "Combine: Warning: Unable to find a file:", bbv_fn
      continue
    if not os.path.isfile(reuse_fn):
      print "Combine: Warning: Unable to find a file:", reuse_fn
      continue
    if not os.path.isfile(insn_fn):
      print "Combine: Warning: Unable to find a file:", insn_fn
      continue

    bbmax=0
    total_reuse_per_phase = []
    reuse_data_per_phase = []
    with gzip.GzipFile(reuse_fn, 'r') as fi:
      print reuse_fn
      for line in fi:
	if line[0] != 'T':
	  continue
	#print line
	data = map(int,line.split(':')[1:])
	#print data
	bbmax = max(data[::2]+[bbmax])
	#print 'max =', bbmax
	total_reuse_per_phase.append(sum(data[1::2]))
	reuse_data_per_phase.append(zip(data[::2],data[1::2]))

    insns_per_phase = []
    try:
      with gzip.GzipFile(insn_fn, 'r') as fi:
        print insn_fn
        for line in fi:
          insns_per_phase.append(sum(map(int,line.split(','))))
      #print insns_per_phase
    except IOError, e:
      write_error(str(e))
      continue

    try:
      out = gzip.GzipFile(out_fn, 'w')
      with gzip.GzipFile(bbv_fn, 'r') as fi:
	lineno = 0
	for line in fi:
	  if line[0] != 'T':
	    continue
	  data = map(int,line.split(':')[1:])
	  new_bbv_nums = map(lambda x:x+bbmax,data[::2])

	  # print out the reuse data and then the updated BBV data
	  out.write('T')
	  for rd_bbid, rd_count in reuse_data_per_phase[lineno]:
	    new_rd_count = (rd_count*insns_per_phase[lineno])/total_reuse_per_phase[lineno]
	    #print '%d: tot_insn=%d total_rd=%d percent=%f old_rd=%d new_rd=%d' % (lineno,insns_per_phase[lineno],total_reuse_per_phase[lineno],float(insns_per_phase[lineno])/total_reuse_per_phase[lineno],rd_count,new_rd_count)
	    out.write(':%d:%d ' % (rd_bbid, new_rd_count))

	  for bbid, bb_data in zip(new_bbv_nums,data[1::2]):
	    out.write(':%d:%d ' % (bbid, bb_data))
	  out.write('\n')

	  lineno+=1
      out.close()
      os.system('touch "%s"' % (os.path.join(out_dir,'success')))
    except IndexError, e:
      write_error(str(e)+('[lineno=%s]'%lineno))
      continue

if __name__ == '__main__':

  def usage(rc = 1):
    print 'combine_data.py --prefix=<prefix> [-h|--help]'
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

  if prefix == None:
    usage()

  run(prefix)
