#!python

import sys
import csv
from collections import deque


## Globals.
trace_file  = sys.argv[1]
appid       = int(sys.argv[2])
gfactor     = float(sys.argv[3])
hits = 0
misses = 0
appID = 19

print "Simulating memcached on app %d with growth factor %f" %(appid,gfactor) 


class RTyp:
  GET, SET, DEL, ADD, INC, STA, OTH = range(7)

class Request:
  def __init__ (self, time, appid, typ, ksz, vsz, kid, hit):
    self.time = time
    self.appid = appid
    self.typ = typ
    self.ksz = ksz
    self.vsz = vsz
    self.kid = kid
    self.hit = hit
  def printReq (self) :
    print "%f %d %d %d %d %d %d" %(self.time, self.appid, self.typ, self.ksz, 
    self.vsz, self.kid, self.hit) 

def procRequest (req):
  x = 3

## Initialize data structures.  






## Parse file and process request.
with open(trace_file, 'rb') as tf:
  reader = csv.reader(tf, delimiter=',')
  for rw in reader:
    req = Request(float(rw[0]),int(rw[1]),int(rw[2]),int(rw[3]),int(rw[4]),
    int(rw[5]),int(rw[6]))   
    if req.hit and req.vsz > 0 and req.appid == appID:
      procRequest(req)
      


class SlabClass:
  def __init__(self, chk):
    self.chunk_size = chk
    self.slabs = [Slab(chk, 0)]
  eviction_queue = deque()
  hits=0
  slabs=1
  def add_slab (self):
    self.slabs.append(Slab(self.chunk_size, ++slabs))
  def kill_slab (self,sid):
    self.slabs.remove(sid)

class Slab:
  def __init__(self, chk, sid):
    self.chunk_size = chk
    self.id = sid
  def __eq__(self, other):
    return self.sid == other.sid
  objs_cached=0
  bytes_cached=0
  hits=0
  def frag ():
    return 1 - (bytes_cached/(objs_cached * chunk_size))

class DataObj:
  def __init__(self, ksz, vsz, objid):
    self.ksz = ksz
    self.vsz = vsz
    self.objid = objid
  def __eq__(self, other):
    return self.objid == other.objid
  def size (self):
    return ksz + vsz
  
