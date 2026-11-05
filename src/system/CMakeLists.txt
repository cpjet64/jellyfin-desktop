if (OPENELEC)
add_subdirectory(openelec)
endif(OPENELEC)

find_all_sources(. SRC)
list(FILTER SRC EXCLUDE REGEX "(^|/)RecoveryBridgeSession\\.(cpp|h)$")
add_sources(${SRC} RecoveryBridgeSession.cpp RecoveryBridgeSession.h)
