add_library(haruhi_execution
    index_maintenance.cxx
    values_executor.cxx
    seq_scan_executor.cxx
    filter_executor.cxx
    projection_executor.cxx
    insert_executor.cxx
    delete_executor.cxx
    index_scan_executor.cxx
    update_executor.cxx
)

target_link_libraries(haruhi_execution PUBLIC
    compile_version_cxx23
    haruhi_catalog
    haruhi_table
    haruhi_record
    haruhi_index
    haruhi_type
)

target_include_directories(haruhi_execution PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)
