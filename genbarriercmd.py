#!/usr/bin/env python

import os, sys, time

def gensimpointdata(bbfile = 'bbvbarrier.bb.gz', fixedlength = 'off', deletesimpointoutput = True, maxk = 20, dim = 15):
  #fixedlength='off' # 'on' or 'off'. Off allows for variable length BBVs

  starttime = time.clock()

  extraopts = []
  simpointcmd = 'simpoint'
  if dim == 0:
    dim = 'noProject'
  if fixedlength == 4:
    fixedlength = 'on'
    extraopts.append('-initkm samp')
    simpointcmd = 'simpoint.nonormalize2'
  elif fixedlength == 3:
    fixedlength = 'on'
    extraopts.append('-initkm samp')
    simpointcmd = 'simpoint.nonormalize'
  elif fixedlength == 2 or fixedlength == 'nonormalize':
    fixedlength = 'on'
    extraopts.append('-initkm ff')
    simpointcmd = 'simpoint.nonormalize'
  elif fixedlength not in ('off','on'):
    fixedlength = {False:'off',True:'on'}[bool(fixedlength)]

  simpointcmd = './%s -loadFVFile "%s" -inputVectorsGzipped -fixedLength %s -maxK %d -dim %s -coveragePct 1.0 -saveSimpoints ./t.simpoints -saveSimpointWeights ./t.weights -saveLabels t.labels -verbose 5 %s' % (simpointcmd, bbfile, fixedlength, maxk, dim, ' '.join(extraopts))
  os.system(simpointcmd)

  simpointdata = {}
  simpointdata['id'] = []
  simpointdata['weights'] = []
  weightsfd = open('t.weights', 'r')
  for idx,line in enumerate(weightsfd):
    weight,num = line.split()
    weight = float(weight)
    num = int(num)
    simpointdata['id'].append(num)
    simpointdata['weights'].append(weight)

  simpointdata['simpoints'] = []
  simpointsfd = open('t.simpoints', 'r')
  for idx,line in enumerate(simpointsfd):
    simpoint,num = line.split()
    simpoint = int(simpoint)
    num = int(num)
    if num != simpointdata['id'][idx]:
      print >> sys.stderr, 'Invalid simpoint index'
      sys.exit(1)
    simpointdata['simpoints'].append(simpoint)

  simpointdata['labels'] = []
  simpointdata['match'] = []
  simpointsfd = open('t.labels', 'r')
  for idx,line in enumerate(simpointsfd):
    label,match = line.split()
    label = int(label)
    match = float(match)
    simpointdata['labels'].append(label)
    simpointdata['match'].append(match)

  if deletesimpointoutput:
    for r in ('t.simpoints', 't.weights', 't.labels'):
      try:
        os.remove(r)
      except:
        pass

  simpointdata['walltime'] = time.clock() - starttime

  #print simpointdata
  return simpointdata

def genbarriercmd(simpointdata):
  barriercmds = []
  for i,sp in enumerate(simpointdata['simpoints']):
    if sp != 0:
      cmd = '0:0:'
    else:
      cmd = ''
    cmd += '%d:1:%d:2' % (sp,sp+1)
    barriercmds.append(cmd)
  #print barriercmds
  return barriercmds

if __name__ == '__main__':
  if sys.argv[1:]:
    for f in sys.argv[1:]:
      if os.path.exists(f):
        genbarriercmd(gensimpointdata(f))
