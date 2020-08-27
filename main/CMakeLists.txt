set(COMPONENT_SRCDIRS ".")
set(COMPONENT_ADD_INCLUDEDIRS ".")
register_component()

string(TIMESTAMP TODAY "%Y%m%d")
component_compile_options(-DCOMPILEDATE="${TODAY}")
