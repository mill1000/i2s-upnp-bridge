idf_component_register(SRCS "mongoose/mongoose.c"
                    INCLUDE_DIRS "mongoose")

target_compile_options(${COMPONENT_LIB} PUBLIC -DMG_ENABLE_SSI=0)