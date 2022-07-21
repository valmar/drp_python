export CPPFLAGS=${CPPFLAGS}" --std=c++11 -Werror"
g++ ${CPPFLAGS} -I/cds/home/v/valmar/Projects/LCLS2/lcls2-spack-drp/install/include/  -g /cds/home/v/valmar/Projects/DrpPython/drp_python/xtc2_patcher.cpp -o /cds/home/v/valmar/Projects/DrpPython/drp_python/xtc2_patcher -L/cds/home/v/valmar/Projects/LCLS2/lcls2-spack-drp/install/lib -lzmq -lrt -lxtc
