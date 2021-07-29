FILE(GLOB_RECURSE app_sources ${COMPONENT_DIR}/*.cpp)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "include"
)
