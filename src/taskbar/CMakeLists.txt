add_sources(TaskbarComponent.h TaskbarComponent.cpp)

if(WIN32)
  add_sources(TaskbarComponentWin.cpp TaskbarMediaTransportWin.cpp TaskbarComponentWin.h SmtcAdmission.h QWinTaskbarStubs.h QWinTaskbarStubs.cpp)
endif(WIN32)
