set(TYPE binary)
set(LOCAL_LIBS plumber dl pss readline)
set(INSTALL yes)

install_includes("${SOURCE_PATH}/pss" "lib/plumber/pss" "*.pss")
