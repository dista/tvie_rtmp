project(tvie_rtmp)

set(LIB_SOURCES readbuffer.cpp readbuffer.h rtmpconnection.cpp
rtmpconnection.h rtmpserver.cpp rtmpserver.h utility.cpp utility.h
writebuffer.cpp writebuffer.h rtmpparser.cpp rtmpparser.h amf0.cpp amf0.h
livereceiveractor.cpp livereceiveractor.h log.h)

set(DEP_LIBS boost_system boost_thread fmp4 avformat avcodec avutil)

add_library(tvie_rtmp SHARED ${LIB_SOURCES})

target_link_libraries(tvie_rtmp ${DEP_LIBS})

add_executable(tvie_rtmp_test main.cpp ${LIB_SOURCES})
target_link_libraries(tvie_rtmp_test ${DEP_LIBS})

