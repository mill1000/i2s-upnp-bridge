idf_component_register(SRCS "mongoose/mongoose.c"
                    INCLUDE_DIRS "mongoose")

target_compile_options(${COMPONENT_LIB} PUBLIC -DMG_ENABLE_CALLBACK_USERDATA=1 -DMG_ENABLE_HTTP_SSI=0 -DMG_ENABLE_HTTP_STREAMING_MULTIPART=1 -DMG_ENABLE_FILESYSTEM=0)
#target_compile_options(${COMPONENT_LIB} PUBLIC -DMG_ENABLE_FILESYSTEM=1 -DMG_ENABLE_HTTP_SSI=0 -DMG_ENABLE_HTTP_STREAMING_MULTIPART=1)