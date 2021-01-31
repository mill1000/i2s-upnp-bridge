idf_component_register(SRC_DIRS "."
                    INCLUDE_DIRS ""
                    EMBED_TXTFILES "web_root/index.html" "web_root/ota.html")

component_compile_options(-std=c++14)