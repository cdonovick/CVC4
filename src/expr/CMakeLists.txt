set(expr_src_files
  array.h
  array_store_all.cpp
  array_store_all.h
  ascription_type.h
  attribute.h
  attribute.cpp
  attribute_internals.h
  attribute_unique_id.h
  chain.h
  emptyset.cpp
  emptyset.h
  expr_iomanip.cpp
  expr_iomanip.h
  expr_manager_scope.h
  expr_stream.h
  kind_map.h
  matcher.h
  node.cpp
  node.h
  node_builder.h
  node_manager.cpp
  node_manager.h
  node_manager_attributes.h
  node_manager_listeners.cpp
  node_manager_listeners.h
  node_self_iterator.h
  node_value.cpp
  node_value.h
  pickle_data.cpp
  pickle_data.h
  pickler.cpp
  pickler.h
  symbol_table.cpp
  symbol_table.h
  type.cpp
  type.h
  type_checker.h
  type_node.cpp
  type_node.h
  variable_type_map.h
  datatype.h
  datatype.cpp
  record.cpp
  record.h
  uninterpreted_constant.cpp
  uninterpreted_constant.h
)

set(expr_gen_src_files
  kind.cpp
  kind.h
  metakind.cpp
  metakind.h
  expr.cpp
  expr.h
  expr_manager.cpp
  expr_manager.h
  type_checker.cpp
  type_properties.h
)

add_library(expr ${expr_src_files} ${expr_gen_src_files})
target_compile_definitions(expr PRIVATE -D__BUILDING_CVC4LIB)

#
# Generate code for kinds.
#
file(GLOB kinds_files ${PROJECT_SOURCE_DIR}/src/theory/*/kinds)

set(mkkind_script ${CMAKE_CURRENT_LIST_DIR}/mkkind)
set(mkmetakind_script ${CMAKE_CURRENT_LIST_DIR}/mkmetakind)
set(mkexpr_script ${CMAKE_CURRENT_LIST_DIR}/mkexpr)

add_custom_command(
  OUTPUT kind.h
  COMMAND
    ${mkkind_script}
    ${CMAKE_CURRENT_LIST_DIR}/kind_template.h
    ${kinds_files}
    > ${CMAKE_CURRENT_BINARY_DIR}/kind.h
  DEPENDS mkkind kind_template.h
)

add_custom_command(
  OUTPUT kind.cpp
  COMMAND
    ${mkkind_script}
    ${CMAKE_CURRENT_LIST_DIR}/kind_template.cpp
    ${kinds_files}
    > ${CMAKE_CURRENT_BINARY_DIR}/kind.cpp
  DEPENDS mkkind kind_template.cpp kind.h
)

add_custom_command(
  OUTPUT type_properties.h
  COMMAND
    ${mkkind_script}
    ${CMAKE_CURRENT_LIST_DIR}/type_properties_template.h
    ${kinds_files}
    > ${CMAKE_CURRENT_BINARY_DIR}/type_properties.h
  DEPENDS mkkind type_properties_template.h
)

add_custom_command(
  OUTPUT metakind.h
  COMMAND
    ${mkmetakind_script}
    ${CMAKE_CURRENT_LIST_DIR}/metakind_template.h
    ${kinds_files}
    > ${CMAKE_CURRENT_BINARY_DIR}/metakind.h
  DEPENDS mkmetakind metakind_template.h
)

add_custom_command(
  OUTPUT metakind.cpp
  COMMAND
    ${mkmetakind_script}
    ${CMAKE_CURRENT_LIST_DIR}/metakind_template.cpp
    ${kinds_files}
    > ${CMAKE_CURRENT_BINARY_DIR}/metakind.cpp
  DEPENDS mkmetakind metakind_template.cpp metakind.h
)

add_custom_command(
  OUTPUT expr.h
  COMMAND
    ${mkexpr_script}
    ${CMAKE_CURRENT_LIST_DIR}/expr_template.h
    ${kinds_files}
    > ${CMAKE_CURRENT_BINARY_DIR}/expr.h
  DEPENDS mkexpr expr_template.h kind.h
)

add_custom_command(
  OUTPUT expr.cpp
  COMMAND
    ${mkexpr_script}
    ${CMAKE_CURRENT_LIST_DIR}/expr_template.cpp
    ${kinds_files}
    > ${CMAKE_CURRENT_BINARY_DIR}/expr.cpp
  DEPENDS mkexpr expr_template.cpp expr.h
)

add_custom_command(
  OUTPUT expr_manager.h
  COMMAND
    ${mkexpr_script}
    ${CMAKE_CURRENT_LIST_DIR}/expr_manager_template.h
    ${kinds_files}
    > ${CMAKE_CURRENT_BINARY_DIR}/expr_manager.h
  DEPENDS mkexpr expr_manager_template.h expr.h
)

add_custom_command(
  OUTPUT expr_manager.cpp
  COMMAND
    ${mkexpr_script}
    ${CMAKE_CURRENT_LIST_DIR}/expr_manager_template.cpp
    ${kinds_files}
    > ${CMAKE_CURRENT_BINARY_DIR}/expr_manager.cpp
  DEPENDS mkexpr expr_manager_template.cpp expr_manager.h
)

add_custom_command(
  OUTPUT type_checker.cpp
  COMMAND
    ${mkexpr_script}
    ${CMAKE_CURRENT_LIST_DIR}/type_checker_template.cpp
    ${kinds_files}
    > ${CMAKE_CURRENT_BINARY_DIR}/type_checker.cpp
  DEPENDS mkexpr type_checker_template.cpp
)