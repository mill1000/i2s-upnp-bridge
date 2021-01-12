idf_component_register(
                    INCLUDE_DIRS "json/single_include/")

target_compile_options(${COMPONENT_LIB} INTERFACE -DJSON_NOEXCEPTION)