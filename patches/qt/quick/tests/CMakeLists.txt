qt_internal_add_test(tst_qtrecoveryaggregate
    SOURCES
        tst_qtrecovery_aggregate.cpp
        ../../../../src/quick/recovery/QtRecoveryQuick.cpp
    INCLUDE_DIRECTORIES
        ../../../../src/quick/recovery
    DEFINES
        QT_RECOVERY_UNIT_TEST
    LIBRARIES
        Qt::Quick
        Qt::GuiPrivate
)

qt_internal_add_test(tst_qtrecoverydrawsets
    SOURCES tst_qtrecovery_drawsets.cpp
    LIBRARIES Qt::QuickPrivate
)
