export CPPFLAGS=${CPPFLAGS}" --std=c++11 -Werror"
${CXX} ${CXXFLAGS} ${CPPFLAGS} -I/cds/home/v/valmar/Projects/LCLS2/lcls2/install/include/  -g /cds/home/v/valmar/Projects/DrpPython/drp_python/drp_xtc_server.cpp -o /cds/home/v/valmar/Projects/DrpPython/drp_python/drp_xtc_server -L/cds/home/v/valmar/Projects/LCLS2/lcls2/install/lib -lzmq -lrt -lxtc
