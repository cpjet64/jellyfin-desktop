if (OPENELEC)
add_subdirectory(openelec)
endif(OPENELEC)

find_all_sources(. SRC)
list(FILTER SRC EXCLUDE REGEX "(^|/)Recovery(BridgeSession|InitialOpening)\\.(cpp|h)$")
add_sources(${SRC} RecoveryBridgeSession.cpp RecoveryBridgeSession.h RecoveryInitialOpening.cpp RecoveryInitialOpening.h)
