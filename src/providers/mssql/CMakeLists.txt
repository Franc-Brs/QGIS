set(MSSQL_SRCS
  qgsmssqlconnection.cpp
  qgsmssqlprovider.cpp
  qgsmssqlproviderconnection.cpp
  qgsmssqlgeometryparser.cpp
  qgsmssqltablemodel.cpp
  qgsmssqldataitems.cpp
  qgsmssqlexpressioncompiler.cpp
  qgsmssqlfeatureiterator.cpp
  qgsmssqlgeomcolumntypethread.cpp
)

if (WITH_GUI)
  set(MSSQL_SRCS ${MSSQL_SRCS}
    qgsmssqlprovidergui.cpp
    qgsmssqldataitemguiprovider.cpp
    qgsmssqlsourceselect.cpp
    qgsmssqlnewconnection.cpp
  )
endif()

set(MSSQL_HDRS
  qgsmssqlexpressioncompiler.h
)

########################################################
# Build

include_directories(

  ${CMAKE_BINARY_DIR}/src/ui
)


add_library(provider_mssql MODULE ${MSSQL_SRCS} ${MSSQL_HDRS})

# require c++17
target_compile_features(provider_mssql PRIVATE cxx_std_17)

target_link_libraries(provider_mssql
  qgis_core
  ${QT_VERSION_BASE}::Sql
)

if (WITH_GUI)
  target_link_libraries (provider_mssql
    qgis_gui
  )
  add_dependencies(provider_mssql ui)
endif()

########################################################
# Install

install (TARGETS provider_mssql
  RUNTIME DESTINATION ${QGIS_PLUGIN_DIR}
  LIBRARY DESTINATION ${QGIS_PLUGIN_DIR})

