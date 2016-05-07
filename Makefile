ifeq ($(SNIPER_ROOT),)
 $(error "Please set $$SNIPER_ROOT")
endif
ifeq ($(PIN_ROOT),)
 ifneq ($(wildcard $(SNIPER_ROOT)/pin_kit/extras/pinplay/examples/pinplay-driver.cpp),)
  PIN_ROOT=$(SNIPER_ROOT)/pin_kit
  export PIN_ROOT
 endif
endif
ifeq ($(PIN_ROOT),)
 $(error "Please set $$PIN_ROOT")
endif

PINTOOL_ICOUNT_DIR=tool_barrier_icount
PINTOOL_BBV_DIR=tool_barrier_bbv
PINTOOL_REUSE_DISTANCE_DIR=tool_barrier_reuse_distance

PINTOOL_ICOUNT=tool_barrier_icount/barrier_icount.so
export PINTOOL_ICOUNT
PINTOOL_BBV=tool_barrier_bbv/tool_barrier_bbv.so
export PINTOOL_BBV
PINTOOL_REUSE_DISTANCE=tool_barrier_reuse_distance/tool_barrier_reuse_distance.so
export PINTOOL_REUSE_DISTANCE

all: $(PINTOOL_ICOUNT) $(PINTOOL_BBV) $(PINTOOL_REUSE_DISTANCE) matrix-omp simpoint
	./barrierpoint.py -- ./matrix-omp

$(PINTOOL_ICOUNT):
	make -C $(PINTOOL_ICOUNT_DIR)

$(PINTOOL_BBV):
	make -C $(PINTOOL_BBV_DIR)

$(PINTOOL_REUSE_DISTANCE):
	make -C $(PINTOOL_REUSE_DISTANCE_DIR)

$(PIN_ROOT)/extras/pinplay/bin/intel64/pinplay-driver.so:
	make -C $(PIN_ROOT)/extras/pinplay/examples

simpoint:
	wget -O - http://cseweb.ucsd.edu/~calder/simpoint/releases/SimPoint.3.2.tar.gz | tar -x -f - -z
	patch -p0 < simpoint_modern_gcc.patch
	make -C SimPoint.3.2
	ln -s SimPoint.3.2/bin/simpoint ./simpoint

matrix-omp: matrix-omp.cpp $(SNIPER_ROOT)/include/sim_api.h $(PIN_ROOT)/extras/pinplay/bin/intel64/pinplay-driver.so
	$(CXX) -g -O3 -fopenmp -I$(SNIPER_ROOT)/include -o matrix-omp matrix-omp.cpp

clean:
	$(MAKE) -C $(PINTOOL_ICOUNT_DIR) clean
	$(MAKE) -C $(PINTOOL_BBV_DIR) clean
	$(MAKE) -C $(PINTOOL_REUSE_DISTANCE_DIR) clean
	rm -rf work ./matrix-omp pintool.log

distclean: clean
	rm -rf SimPoint.3.2 ./simpoint

.PHONY: clean distclean
